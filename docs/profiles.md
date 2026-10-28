# Work and learn profiles

Kremen 0.4.5 introduces two checking profiles for the full language. `work` is the default. `learn` adds five explicit requirements after the ordinary checker has accepted the program. Both use the same syntax, types, borrowing rules, C backend, runtime checks, and optimization settings.

A learning error asks you to make a decision visible in source. It does not mean the work profile permits invalid memory access. For source accepted in both profiles, the emitted C is byte-identical when the other options and inputs are the same.

## Choose a profile

```sh
kremen check main.kr --mode=work
kremen check main.kr --mode=learn
kremen build main.kr --mode=learn -O3 -o build/app
kremen run examples/learn/checked_input.kr --mode=learn
kremen emit-c main.kr --mode=learn -o build/main.c
```

Omitting `--mode` selects `work`. The option has the exact form `--mode=work` or `--mode=learn`. If supplied more than once, the last occurrence wins. Put all compiler options before `--` when using the end-of-options delimiter.

The selected profile applies to every implementation loaded for the program, including local imports. A helper function in another file cannot silently opt out. Headers still undergo the shared signature checks. `fmt` and `explain` do not run a semantic profile and reject `--mode`; `kremen explain L003` works by itself.

Profiles currently belong to the full compiler. The self-hosted subset does not implement them. There is no persistent project-profile configuration, lesson runner, or browser execution in this release.

## The five learning requirements

| Code | Requirement | Example of the decision being made explicit |
| --- | --- | --- |
| `L001` | Initialize local variables explicitly | `var count i64 = 0` chooses zero instead of relying on implicit zero initialization |
| `L002` | Observe or propagate result-bearing values | Inspect `!T`, handle it with `match` or `catch`, or propagate it with `?` |
| `L003` | Bind and use failure codes in `catch` and result `fail` patterns | `catch code => -code`, or `fail(code) => { println(code) }` |
| `L004` | List every `match` case explicitly | Write every enum variant, or separate `ok` and `fail` result arms |
| `L005` | Use fallible integer input | Choose `readIntResult()` and an explicit result path instead of `readInt()` |

Local type inference remains available. The profile does not require repetitive annotations for every local variable. Unused ordinary values can still be acknowledged with an underscore under the shared language rules.

`L002` covers explicit `_ = value`, unread underscore-prefixed bindings, and ignored tuple bindings. It checks results held by value inside arrays, tuples, structs, and every enum payload shape. This check is conservative: an enum type with a result-bearing variant participates even when a particular expression constructs another variant. It stops at slice descriptors and addresses rather than examining separately reachable storage. Shared type graphs are cached during this analysis.

`L003` requires the bound code to be read. A spelling such as `_code` is allowed when the code is actually used; the underscore does not excuse ignoring it. `?` can propagate a failure unchanged without a local code binding.

These are structural checks. Reading a result's status or a code does not prove that the application's policy is sensible, that every overwrite was preceded by handling, or that every failure is meaningfully handled on every path. `L005` concerns integer input, not all I/O effects. Contracts and richer flow analysis need separate rules and tests.

## Turn a hidden fallback into a visible decision

This complete program is valid in `work`:

```go
func main() {
    var count = readIntResult() catch 0
    println(count)
}
```

In `learn`, the `catch` token receives `L003`: the failure code was discarded. A fallback of zero can be an intentional application policy, but the exercise requires you to retain the failure information in the expression. Run `kremen explain L003` for the explanation and a small example.

For a fuller example, save the following program, or use the same program in the formatted [checked-input example](../examples/learn/checked_input.kr):

```go
package main

func readPositiveCount() !i64 {
    var count = readIntResult()?
    if count <= 0 {
        return fail(1001)
    }
    return ok(count)
}

func main() {
    var input = readPositiveCount()
    match input {
        ok(count) => {
            println(100 / count)
        }
        fail(code) => {
            if code == 1001 {
                println("Count must be greater than zero.")
            } else {
                println("Cannot read count:", errorMessage(code))
            }
        }
    }
}
```

Enter `4` to get `25`. Zero and negative input produce the application message. Invalid integer input and EOF produce a separate input-error message. `1001` is this example's application error, so it is handled before passing OS error codes to `errorMessage`.

The helper propagates input errors with `?`, validates the application's positive-count requirement, and returns a result. `main` lists both result cases and uses the failure code. All five learning requirements are satisfied.

## Safety and runtime behavior

Both profiles reject the same invalid types, conflicting borrows, forbidden address escape, and unsupported worker handles before code generation. Both retain runtime bounds, overflow, stale-slice, failed-result, and null-pointer checks. A runtime failure still runs the calling thread's deferred cleanup under the ordinary rules.

The current learning profile does not prove that a variable divisor is nonzero or that an algorithm terminates. The example makes its positive-count check explicit in source; runtime division checks remain present. No profile disables safety checks to make a program faster.

The shared checker runs first. A program with an ordinary type or borrowing error reports that error before additional learning requirements. JSON diagnostics include `mode`, identifying the active profile when the error was reported, and `explain` links learning errors to the same offline catalog as other errors.

Read [Safety and memory](safety.md) for the broader language boundary and the [roadmap](roadmap.md) for future teaching work.
