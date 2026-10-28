# Understanding errors

An error message is a report about a specific operation, not a judgment about your ability to program. Read the first line, find the marked code, and make one small change. Then run `check` again.

This page describes 0.4.5. Examples shorten directory names for readability; the full compiler normally prints the resolved source path, with unsafe terminal characters escaped.

## Read the location first

Consider this complete, intentionally invalid program:

```go
func main() {
    var count i64 = "many"
    println(count)
}
```

Its diagnostic looks like this:

```text
example.kr:2:21: error[E110]: type mismatch: expected i64, got string
   |
 1 | func main() {
 2 |     var count i64 = "many"
   |                     ^~~~~~
 3 |     println(count)
 help: Check the value and its declared type; Kremen does not convert types implicitly.
 explain: kremen explain E110
```

Read the first line from left to right:

- `example.kr` is the source file.
- `2:21` means line 2, column 21. Both start at 1.
- `error[E110]` gives the diagnostic category and code.
- The message says what was required and what the compiler found.

The numbered lines are your source, including nearby context when available. `^` marks the beginning of the token, and `~` continues its underline. Here the variable requires an integer, but `"many"` is a string. If you meant a number, write `var count i64 = 5`. If you meant text, use `var count string = "many"` instead. Choose the type that matches your program's intent.

A code such as `E110` is useful when reporting a problem, but it is not a substitute for reading the message. Several checks share a code. An `error[W001]` is also fatal: Kremen treats its warning checks as errors by default.

## Ask for an explanation offline

```sh
kremen explain E131
kremen explain W001
kremen explain --list
```

`explain` prints why that category occurs, what to check, and a small example. The catalog is built into the full compiler and works without a project, runtime header, C compiler, network, or account. Running `kremen explain` without a code lists the catalog too. Some examples are code fragments intended to be placed inside a function.

The original error message identifies the particular check. For example, `E110` covers both syntax and type disagreements, so its general explanation does not replace the marked source and message. The command supports 44 categories: the preceding 39 native categories, including fatal `W001`, and learning requirements `L001` through `L005`. The self-hosted subset has its own diagnostic scheme.

An unknown code is a command-line error, not a guessed explanation. Select `--mode=learn` on a semantic command to enable the first five additional requirements. [Work and learn profiles](profiles.md) describes their scope. A lesson runner and persistent project-profile configuration remain planned work in the [roadmap](roadmap.md).

## Common mistakes and their fixes

| Message or symptom | What to check |
| --- | --- |
| `unknown variable` / `unknown function` | Spelling, whether the declaration exists in this scope, and the import qualifier |
| `type mismatch` | The declared type and the supplied value; integer conversions must be explicit |
| `Expected ...` | A missing delimiter or an incomplete expression, including the preceding line |
| `unused variable or parameter` | Whether you forgot to use it; prefix an intentionally unused name with `_` |
| `shadows an existing or reserved name` | Rename the new binding, or use `=` if you meant to update an existing one |
| `requires an unsafe block` | Whether you intended a manual memory operation; keep it in a small `unsafe` block |
| `while it is borrowed` | A reference or deferred call still holds a loan on the variable |
| A non-exhaustive `match` | Add the missing variants, or a final `_` arm when a fallback is appropriate |
| `ok/fail requires a contextual result type` | Supply a result annotation, for example `var answer !i64 = ok(42)` |
| A discarded result | Handle it with `match`, `catch`, or `?`; `_ = call()` explicitly discards it when intended |

A column points at the token associated with the failed check. For a compound operation it may mark the operator or the start of the indexed value. It is a useful starting point, not a claim that changing only that token is always the right fix.

For an unknown name, the checker can suggest a nearby spelling when it finds a unique close candidate. This includes variables, functions, imported functions, types, fields, methods, and enum variants. A suggestion is help for a likely typo, not an automatic rename. Check that the proposed name is actually the one you intended. A misspelled imported function is reported as a function lookup failure in that module, rather than incorrectly blaming the module's qualifier as an unknown variable.

Unknown types are marked at the type annotation itself. Qualified annotations include the complete marked name, and their suggestions search the imported module. A misspelling such as `vec.Vectro` can suggest `vec.Vector`; it must not suggest an unrelated primitive as though that primitive belonged to `vec`.

## Follow a borrow back to where it began

This complete example is intentionally rejected:

```go
func main() {
    var value = 1
    var reference = &mut value
    println(*reference)
    value = 2
}
```

The assignment conflicts with the mutable reference. The diagnostic reports the assignment and adds an underlined note at the source location where the conflicting borrow began. Borrowing currently lasts until the end of the reference's block, even after its last use.

End the loan before assigning the original value:

```go
func main() {
    var value = 1
    {
        var reference = &mut value
        println(*reference)
    }
    value = 2
    println(value)
}
```

The extra block is meaningful: its closing brace ends the reference's lifetime. The compiler also reports a previous declaration's location for conflicting local names. Read those notes alongside the primary error.

## Errors that happen while a program runs

Some values are only known at runtime. In this complete example, the index comes from input:

```go
func main() {
    var values = [2]i64{4, 5}
    var index = readInt()
    println(values[index])
}
```

Input `9` is outside this array. The program reports a `runtime error` with the `.kr` file, line, column, and source excerpt. The source location belongs to the failed operation, including operations in imported files. Integer overflow, stale slices, failed-result access, `assert(false)`, and user `panic` use the same location mechanism.

The full compiler embeds bounded excerpts and locations in the executable. Moving it, changing the original source file, or removing that source does not change its saved diagnostic. Rebuild the executable to update those locations. This data adds to the generated C and binary size and retains build-time source paths and small pieces of code. It is not a stack trace, and it does not list the chain of callers.

After a runtime safety failure or user panic, that thread's active deferred calls run. A main-thread panic joins outstanding children and exits unsuccessfully. A worker panic includes the worker's starting integer argument in its diagnostic, stops that worker, and makes its creator's join return `ECANCELED`. There is no recovery inside the panicking thread. Use results for expected errors such as missing files or invalid user input. See [Working with threads](threads.md).

## Long lines, tabs, Unicode, and end of file

The compiler shows a bounded window around a token on a long line. `...` means text was omitted. Clipping does not change the line or column in the header. The excerpt is at most 160 source bytes before display expansion and clipping markers.

Header columns count UTF-8 bytes; a tab counts as one byte there. Excerpts expand tabs to four-column stops within the visible window. Underlines count UTF-8 code points as one cell. Wide characters and combining marks may occupy a different width in your terminal, so their visual alignment is approximate. ASCII code and ordinary single-width text align predictably.

An error at the end of a file can point at a blank line. For example, an open `{` still needs its closing `}` even though there is no token left to underline. Look back at the opening construct.

If the failing source was imported, additional `imported from here` notes show the source locations that led to it. This helps distinguish a problem in the entry file from one in a dependency. A literal NUL byte in source is also reported at its actual byte position; write an escape such as `\0` inside a string instead of inserting a zero byte into the source file itself.

The frontend makes terminal controls, direction-changing Unicode, and invalid bytes visible in file names and messages. Source excerpts use `?` cells for unsafe scalars. Ordinary UTF-8 remains readable; byte ranges still refer to the original source. Text and JSON messages use a 4,096-byte formatting buffer, with `...` when a longer message is clipped. The native runtime applies the same path-display policy to its saved source locations; program-owned output strings and panic messages keep their own semantics.

## Structured errors for editor tools

The native compiler can write its frontend diagnostic as JSON:

```sh
kremen check example.kr --diagnostic-format=json 2> error.json
```

Use `--diagnostic-format=text` for the default readable output. The option is accepted by full-language `check`, `build`, `run`, `emit-c`, and `fmt`. A successful check still writes its normal success message to stdout; it does not produce an error object. A frontend failure emits one object on stderr and exits unsuccessfully.

| Field | Meaning |
| --- | --- |
| `kind`, `code`, `message` | Error category, diagnostic code, and readable explanation |
| `message_truncated` | Whether a very long message had to be shortened |
| `file`, `line`, `column` | Source path and one-based byte position |
| `end_line`, `end_column` | Exclusive end of the marked byte range |
| `offset`, `length` | Zero-based byte offset in the file, and marked byte count |
| `excerpt` | Bounded displayed source window, with tabs expanded |
| `caret`, `underline_width` | Zero-based display-cell offset and underline width in that excerpt |
| `hint` | Optional help text, or `null` |
| `explain` | Offline command for that diagnostic category, or `null`; added in 0.4.4 |
| `mode` | Active full-language checking profile, `work` or `learn`; added in 0.4.5 |
| `related` | Earlier declaration/borrow locations, with their explanatory messages |
| `imports` | Import-origin locations leading to the failing source |

Editor integrations should use the file/range fields to select source. The displayed excerpt can be clipped and should not be used to reconstruct the whole line. JSON strings escape control characters and invalid UTF-8 bytes so malformed input still produces a valid error record. Visual caret width retains the Unicode limits explained above.

JSON also escapes direction-changing characters. Invalid UTF-8 bytes are reported as byte-valued Unicode escapes; a decoded JSON string alone cannot generally round-trip an invalid-UTF-8 filesystem name. Valid UTF-8 names retain their decoded path. The byte offsets and lengths describe the input file, including when its content is malformed.

This option covers Kremen frontend diagnostics and command validation, including unknown commands, missing or extra sources, invalid formatter modes, invalid formatter input, and protected output paths. It does not convert external C-compiler output, program stdout, runtime errors, every late OS failure, or formatting-drift reports into JSON. The separate self-hosted subset also keeps its own text diagnostics. A tool invoking `build` or `run` must distinguish the frontend from later stages rather than assuming every stderr line is a JSON object. Successful `explain` output is ordinary text on stdout even when this diagnostic option is selected.

## Which component produced the error?

The full `kremen check` command reports Kremen syntax and type errors without calling a C compiler. It stops at the first frontend error. Fix that error and check again; collecting further errors from an invalid tree could produce misleading follow-up messages.

`build` and `run` also invoke a C compiler. Messages naming generated `.c` files come from that backend. A valid Kremen program that produces invalid C may indicate a compiler defect. Keep the original source and save generated C with `emit-c` when reporting it. An unavailable `cc`, unwritable output directory, or missing system tool is a toolchain setup issue.

The separate `kremen selfhost` compiler reports its input path, line, column, and a source excerpt for frontend errors. Its type checks now point at the expression rather than the next token. Arithmetic failures in programs built by that subset retain a source basename and line/column; they do not gain the full compiler's embedded excerpts. C-backend errors still refer to generated C.

For a useful bug report, include `kremen --version`, the command you ran, a small complete source file, its necessary input, and the full diagnostic. If the problem involves imports, include the imported files and the chosen `--root`.
