# Kremen language reference

This page describes the full language accepted by `kremen` in 0.4.6. It is a reference for rules and built-ins. If the terms are unfamiliar, start with [the tutorial](tutorial.md). The compiler under `selfhost/` accepts a smaller language; its [reference](../selfhost/README.md) is separate.

Examples that show only a declaration or block are fragments to put inside a program. Complete programs include `func main()`.

Semantic commands default to `--mode=work`. `--mode=learn` adds the five requirements in [Work and learn profiles](profiles.md) after the shared checker succeeds. Accepted source has the same runtime behavior and C output in both profiles with the same options. The ordinary binding and match rules below describe work; learn additionally requires explicit initializers, result observation, used failure codes, explicit match cases, and fallible integer input.

## Files and local imports

| Extension | Purpose |
| --- | --- |
| `.kr` | Kremen source code |
| `.krh` | Kremen API declarations, without function bodies |
| `.c`, `.h` | Compiler/runtime implementation and generated C |
| `.S` | Assembly source processed by the C preprocessor |
| `.o`, `.a` | Native object files and archives |

`package main` is optional at the beginning of a file. Top-level declarations are functions, methods, structs, enums, and imports. The full language has no global variables, nested functions, or closures. The entry point is `func main()` with no arguments or result.

Statements end at a suitable newline or an explicit `;`. Comments use `//` or `/* ... */`. Blocks use braces. Parameter syntax is `name type`; a function's result type follows its parameter list. Omitting the result type means the function returns no value.

An import names a local file or directory:

```go
import "math/vec"
import vectors "math/vec"
```

These are alternative imports: the first uses `vec` as its qualifier; the second uses `vectors`. The path is resolved relative to the importing file's directory. Import a file without its `.kr` extension, or a directory containing multiple `.kr` and `.krh` files. No dependency is downloaded. Cycles and paths outside the project root, including symlink escapes, are rejected. The default root is the entry file's directory; `--root /absolute/project` selects a wider root.

The selected root must be a directory containing the canonical entry-module path and all loaded sources. Directory entries and paired headers receive the same check as the import itself. Filesystem operations performed by the compiled program are separate from this source-loading rule.

A complete module example is in [examples/modules](../examples/modules/main.kr):

```sh
./build/kremen run examples/modules/main.kr
```

A paired `.krh` file is loaded automatically. Its type definitions and function signatures must agree with the implementation. A non-`extern` function declared in a header must have an implementation. Import the `.kr` implementation, not the `.krh` file. Headers are optional; use them when you want a separate API declaration. General C FFI and command-line linking of arbitrary external objects are unfinished.

## Bindings and types

These are alternative local declaration forms:

```go
var count i64 = 4
var zero i64
next := 5
const limit i64 = 10
```

`var` and `:=` create mutable bindings; `const` prevents assignment. A literal without a more specific context defaults to `i64`. A variable needs a type or an initializer; a constant needs an initializer. Unused bindings and parameters are errors unless their names start with `_`. Shadowing a visible or reserved name is rejected. `_ = expression` explicitly discards a value.

| Type | Meaning |
| --- | --- |
| `i8`, `i16`, `i32`, `i64` | Signed integers of the stated bit width |
| `u8`, `u16`, `u32`, `u64` | Unsigned integers of the stated bit width |
| `bool` | `true` or `false` |
| `string` | Immutable byte sequence |
| `[N]T` | Fixed-size array of `T` |
| `[]T` | Slice descriptor for shared dynamic storage |
| `&T`, `&mut T` | Shared read reference or exclusive mutable reference |
| `*T` | Raw pointer |
| `!T` | Success containing `T`, or a positive error code |
| `(T1, T2)` | Tuple with ordered members |
| `arena`, `file` | Checked resource handles |
| `thread` | Creator-owned handle for a native worker thread |

Structs and enums are named types. Integer types do not mix implicitly: `i32(value)` explicitly converts and checks the range. Integer literals may be decimal, hexadecimal (`0xff`), binary (`0b1010`), or octal (`0o17`). Floating point and general user-defined generics are not implemented.

Ordinary values may be zero-initialized. Results, references, pointers, and standalone `arena`/`file`/`thread` handles require initialization. A composite containing a result also cannot be left zero-initialized. The zero value of an enum is its first variant with zeroed payload, provided that payload permits zero initialization.

Strings support `\n`, `\r`, `\t`, `\0`, `\\`, `\"`, `\xNN`, `\uNNNN`, and `\UNNNNNNNN` escapes. Unicode escapes must denote valid scalar values; `\xNN` supplies a byte. Literal source text must use valid UTF-8. Strings may contain NUL bytes. `len(text)` counts bytes, not displayed characters. Mutable byte indexing of strings is not provided.

## Operators and control flow

Arithmetic: `+ - * / %`. Bitwise: `& | ^ ~ << >>`. Comparisons: `== != < <= > >=`. Boolean operators: `&& || !`. Compound assignments and statement forms `++` and `--` are supported. An `if` or loop condition must be `bool`.

Expressions and arguments are evaluated left to right. `&&` and `||` evaluate their right operand only when needed. Integer overflow, invalid shifts, division by zero, and out-of-range conversions are errors; optimization does not remove safety checks.

```go
func main() {
    var remaining = 3
    if remaining > 0 { println("ready") } else { println("empty") }
    for index := 0; index < 3; index++ { println(index) }
    for remaining > 0 { remaining -= 1 }
    for { break }
}
```

`break` leaves the nearest loop. `continue` starts its next iteration, including the post step of a three-clause loop. `return` leaves a function. The compiler rejects unreachable statements and missing returns. A `break` inside `match` still refers to the surrounding loop.

## Arrays, structs, and methods

```go
type Counter struct { value i64 }
func (counter &mut Counter) add(amount i64) { counter.value += amount }
func (counter Counter) current() i64 { return counter.value }

func main() {
    var values = [3]i64{10, 20, 30}
    values[1] = 25
    var counter = Counter{value: values[1]}
    counter.add(2)
    println(counter.current(), len(values))
}
```

Array length is a positive literal and part of its type. Array indices start at zero and are checked. Arrays, structs, enums, and tuples are copied by value. A struct literal supplies every field exactly once.

A value receiver gets a copy. A reference receiver borrows the named local receiver; `&mut` allows changes to it. Methods belong to the declaring module's own struct or enum types. There is no inheritance, interface system, or virtual dispatch.

An array type may have at most 1,000,000 elements; the estimated layout of a single value is limited to 1 MiB. Layout analysis has a depth limit of 128. These are compiler limits, not a total program-memory budget.

## References and raw pointers

This complete example changes a variable through a reference, then through a pointer:

```go
func main() {
    var number = 1
    {
        var reference = &mut number
        *reference += 1
    }
    unsafe {
        var pointer = raw(&mut number)
        *pointer += 1
    }
    println(number)
}
```

A borrow covers the whole named variable. Multiple shared references may coexist; a mutable reference excludes conflicting access. A local reference holds its loan until the end of its lexical block. A temporary argument borrow normally ends after the call; a deferred argument keeps its loan until cleanup.

References and raw pointers cannot be reassigned, returned, or stored inside structs, enums, tuples, results, arrays, or slices. There are no user-specified lifetime parameters. These restrictions are intentionally conservative.

`raw(reference)` and raw-pointer dereferencing require `unsafe`. Converting to a raw pointer preserves the loan. A pointer from `raw(&number)` remains read-only; writes or writable `*T` parameters require provenance from `&mut`. Pointer arithmetic and integer-to-address casts are not provided. A null check cannot prove that arbitrary raw memory is valid.

## Slices and allocation

Inside a function:

```go
unsafe {
    var values = alloc[i64](4)
    defer free(values)
    values[1] = 7
    var view = slice(values, 1, 3)
    println(len(view), view[0])
}
```

`alloc[T](count)` returns a zero-filled `[]T`. `free(values)` requires the original full allocation, not a partial view. Both operations require `unsafe`. Allocating zero-filled results or composites containing results is rejected because a result needs explicit success or failure initialization.

`slice(values, start, end)` creates a view without copying data; start is inclusive and end exclusive. `len` returns `i64`. Descriptor copies share an owner. Freeing invalidates every copy; stale access, repeated release, and invalid bounds produce runtime errors. Assignment rechecks a slice owner after evaluating its right-hand side, since a call on that side could have freed or reset it. There is no automatic growth, `append`, or garbage collector.

## Error results

```go
func checked(value i64) !i64 {
    if value < 0 { return fail(17) }
    return ok(value * 2)
}
func forwarded(value i64) !i64 {
    var answer = checked(value)?
    return ok(answer + 1)
}
func main() {
    println(forwarded(3) catch 0)
    println(checked(-1) catch code => code + 1)
}
```

`!T` has read-only fields `.ok` (`bool`), `.value` (`T`), and `.code` (`i64`, zero on success). `ok` and `fail` need an expected `!T` type, obtained from a return type or annotation such as `var answer !i64 = ok(42)`. A failure code must be positive. Accessing `.value` on failure stops the program.

`?` extracts success or returns the same failure code from the surrounding result-returning function. Its success type may differ from that function's success type. `catch fallback` extracts success or lazily evaluates a replacement of type `T`. `catch code => fallback` additionally binds an `i64` code inside that fallback. Each operand and selected branch is evaluated once.

You can also use `match result { ok(value) => { ... }; fail(code) => { ... } }`. Results use ordinary values and branches, without language-level exception unwinding.

## Input, output, and fatal errors

| Built-in | Behavior |
| --- | --- |
| `print(values...)` | Print integers, booleans, or strings, with spaces between arguments |
| `println(values...)` | As above, followed by a newline |
| `readInt()` | Read an `i64`; stop on invalid input |
| `readIntResult()` | Return `!i64`; codes 1 EOF, 2 format, 3 range, 4 stream error |
| `readLine()` | Return a line as `string` |
| `len(value)` | Return array, slice, or string length as `i64` |
| `assert(condition)` | Stop with a diagnostic if false |
| `panic(message)` | Report a string message and run the calling thread's cleanup; main exits, while a worker's join fails |

Printing a struct or enum directly is unsupported; print its individual fields or match its variants.

Output can be buffered. Full-language programs check stdout after worker joining and cleanup at process termination; a failed final flush produces a runtime error and an unsuccessful exit. A delayed flush error has no individual print-call location. This does not change default host signal handling such as `SIGPIPE`.

The four `readIntResult` codes are input-specific values, not OS `errno` values. Match them explicitly, as the [checked-input example](../examples/learn/checked_input.kr) does. Use `errorMessage` for the OS errors returned by filesystem and worker operations.

An array's length is fixed by its type, but `len(expression)` still evaluates the expression and its safety checks. A call inside the expression runs once, and a bad index or failed-result access still fails. Querying an existing fixed array's length does not require copying the array's elements.

## Defer and cleanup

```go
func main() {
    var value = 1
    defer println("captured", value)
    value = 2
    println(value)
}
```

This prints `2`, then `captured 1`. `defer` requires a void call. Its arguments and method receiver are evaluated at registration. The call runs later, in reverse registration order, when the current lexical block exits.

Cleanup runs on ordinary block exit, `return`, `?`, `break`, `continue`, user `panic`, and runtime safety failure. Each loop iteration has its own registrations. A return value is computed before cleanup. A reference captured by `defer` keeps its borrow until cleanup.

This is explicit cleanup; there are no implicit user-defined destructors. There is no `recover` or panic-catching mechanism. Expected failure belongs in `!T`, `catch`, `?`, or `match`. In a worker thread, panic runs that worker's cleanup and makes its join return `ECANCELED`; see [threads](#threads). Cleanup is not guaranteed after SIGKILL, process corruption, or a C-toolchain crash.

## Threads

The full runtime uses native POSIX threads. A worker is a named, non-receiver function with exactly one `i64` parameter and an `i64` result. Starting a worker copies its integer argument; joining retrieves its integer result.

| Call | Result and purpose |
| --- | --- |
| `threadSpawn(worker, argument i64)` | `!thread`: start the named worker |
| `threadJoin(handle thread)` | `!i64`: wait and retrieve its result |
| `threadClose(handle thread)` | Void, idempotent wait suitable for `defer` |

```go
func square(value i64) i64 { return value * value }

func compute() !i64 {
    var first = threadSpawn(square, 6)?
    defer threadClose(first)
    var second = threadSpawn(square, 8)?
    defer threadClose(second)
    var left = threadJoin(first)?
    var right = threadJoin(second)?
    return ok(left + right)
}

func main() {
    println(compute() catch code => -code)
}
```

On success this prints `100`. Spawn and join failures use positive OS error codes. A worker panic is reported at its source location, cleans up that worker, and makes its creator's join fail with `ECANCELED`. An unhandled failed child makes creator cleanup fail; failure from an explicitly joined child can be handled through ordinary results.

Only the creator may join or close a handle. Copies refer to the same worker state. A second explicit join returns `EBADF`; `threadClose` is a no-op after a completed join. Outstanding children are joined before the creator's thread resources are reclaimed, including at main-thread exit. There is no forced cancellation: a nonterminating worker can keep its creator waiting.

Thread handles can be held in local variables and local `!thread` results, or passed to a helper executing in the same creator thread. They cannot be returned, borrowed, placed in ordinary composites, or nested inside further results. The worker interface cannot receive references, pointers, strings, slices, or other resource handles. Worker functions cannot capture local variables. Each worker has its own runtime allocation, arena, file, and cleanup bookkeeping. Limits are 256 unjoined children and 65,536 retained thread handles per creator. Generated `print`/`println` calls and input operations are serialized; filesystem effects on shared paths still need a program-level design.

See [Working with threads](threads.md) for a step-by-step explanation and the current boundaries. Mutexes, atomics, channels, detached jobs, and timed joins are not part of this release. Full-language generated C must be compiled and linked with `-pthread`.

## Arenas

Inside a function:

```go
arenablock memory {
    unsafe {
        var values = arenaAlloc[(i64, bool)](memory, 2)
        values[0] = (7, true)
        println(values[0].0)
        arenaReset(memory)
    }
}
```

`arenablock name` creates an arena and registers its release at block exit. Deferred calls in the body run before the automatic arena release. Manual `arenaNew()`, `arenaAlloc[T](arena, count)`, `arenaReset(arena)`, and `arenaFree(arena)` require `unsafe`.

Allocations are zero-filled regions in bump-allocated chunks, usually 64 KiB. Reset releases all chunks and invalidates old slices; the arena remains available for new allocation. Free also invalidates the handle. Do not individually free arena slices. A slice escaping the block does not extend the arena's lifetime: later access traps. Repeated manual free is an error; automatic block cleanup after a manual free is idempotent.

## Enums and matching

```go
type Event enum {
    Empty
    Number(value i64)
    Pair(number i64, text string)
}
func show(event Event) {
    match event {
        Empty => { println("empty") }
        Number(value) => { println(value) }
        Event.Pair(value, _) => { println(value) }
    }
}
func main() { show(Event.Number(42)) }
```

An enum is a tagged union stored as a value, without a separate allocation. It has between 1 and 256 variants. A payload may contain ordinary composite values, but not references or pointers. Even a payload-free constructor uses `()`. Directly recursive value layouts are rejected; recursion through slices need not create an infinite layout.

`match` is a statement. Cover every variant, or end with `_ => { ... }`. Duplicate variants, wrong payload arity, and arms after `_` are errors. Pattern names bind immutable payload copies; `_` discards one. Qualified patterns such as `Event.Number` or `module.Event.Number` are supported. Nested patterns, literal patterns, and guards are not implemented.

## Tuples

```go
func pair() (i64, string) { return (7, "seven") }
func main() {
    var (number, text) = pair()
    var tuple = (number, text)
    tuple.0 += 1
    println(tuple[0], tuple.1)
}
```

Tuple types are structural: order, member types, and arity matter. Access uses `.0`, `.1`, or a constant index. A dynamic index is not allowed because members may have different types. The maximum arity is 32. `()` is the empty unit tuple; `(value,)` and `(T,)` are one-member forms; `(value)` merely groups an expression.

`var (a, b) = expression` and `const (a, b) = expression` evaluate once and require matching arity. `_` discards a member. Assignment copies the tuple by value. Address types cannot be tuple members.

## Filesystem built-ins

No import is needed for the current file API. Filesystem failures use positive OS `errno` values; their numbers vary by platform. `errorMessage(code)` describes them. NUL bytes in paths are rejected. File contents can contain NUL. Other path behavior, permissions, and symlinks follow the host OS.

| Call | Result and purpose |
| --- | --- |
| `fsRead(path string)` | `!string`: read a whole file |
| `fsWrite(path string, data string)` | `!bool`: create or replace contents |
| `fsAppend(path string, data string)` | `!bool`: append contents |
| `fsExists(path string)` | `!bool`: a missing path is successful `false` |
| `fsIsDir(path string)` | `!bool`: test directory status |
| `fsSize(path string)` | `!i64`: file size |
| `fsMkdir(path string)` | `!bool`: create one directory |
| `fsRemove(path string)` | `!bool`: remove a file or empty directory |
| `fsRename(from string, to string)` | `!bool`: rename a path |
| `fsList(path string)` | `![]string`: names in byte-sorted order, without `.` or `..` |
| `fsCwd()` | `!string`: current directory |
| `fsOpen(path string, mode string)` | `!file`: modes `r`, `w`, `a`, `rb`, `wb`, `ab` |
| `fsClose(handle file)` | `!bool`: close, with an error on repeated close |
| `fsReadAll(handle file)` | `!string`: read from current position to EOF |
| `fsWriteAll(handle file, data string)` | `!i64`: bytes written |
| `fileClose(handle file)` | Void, idempotent cleanup suitable for `defer` |
| `errorMessage(code i64)` | `string`: OS error description |

Handle copies refer to the same resource. Closing invalidates all copies; later operations return an error. `fileClose` ignores an already closed handle, but an actual close failure panics. Closure is final even if `fsClose` reports an OS error. Writes may partially modify a file before failure and do not promise `fsync` durability.

A read is limited to 64 MiB; directory listing is limited to 1,000,000 entries. Free a successful listing's slice explicitly in `unsafe`; its name strings stay managed until process exit. Input/file strings also stay managed until exit. The filesystem API is not confined to the import root.

## Tools and diagnostics

```sh
kremen check source.kr
kremen build source.kr -O3 -o program
kremen run source.kr
kremen emit-c source.kr -o program.c
kremen fmt -w source.kr api.krh
kremen fmt --check source.kr api.krh
kremen check source.kr --diagnostic-format=json
```

`-O0` through `-O3` select optimization, with `-O3` as default. `--cc /path/to/cc` selects one compiler executable; `CC` provides the default. `--` ends option parsing, for example `kremen check -- -source.kr`. `-o` is accepted by `build` and `emit-c`, not by `check` or `run`. Full-language `emit-c` writes stdout if `-o` is omitted. The separate self-hosted command requires an output file for `emit-c`.

`fmt` reads stdin or one file and writes stdout. `-w` rewrites files; `--check` reports layout differences without writing. Multiple files require `-w` or `--check`.

Compile errors include path, line, byte column, code, source context, token underline, and relevant earlier locations. `--diagnostic-format=json` selects structured frontend diagnostics; `--diagnostic-format=text` selects the default human-readable output. Runtime errors include source location and an embedded bounded excerpt; they are not stack traces. The JSON option does not convert C-backend or runtime messages. See [Understanding errors](diagnostics.md). The compiler limits a source file to 4 MiB, its arena to 512 MiB, tokens per file to 1,000,000, source files to 512, modules to 128, and parse/AST depth to 256. Safety boundaries are explained in [Safety and memory](safety.md).
