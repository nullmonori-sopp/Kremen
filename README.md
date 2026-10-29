# Kremen 0.4.6

Kremen is a small compiled language for learning and experimenting with systems programming. Its syntax is inspired by Go. It has explicit integer types, structs and methods, checked references, raw pointers, arrays, slices, results, scoped resource cleanup, and native worker threads.

The main compiler is written in C11. It translates Kremen into C and asks a C compiler to produce an executable. A separate, smaller compiler and command-line driver are written in Kremen and can rebuild themselves. The [self-hosting guide](docs/selfhosting.md) explains that distinction.

**New to programming? Start with [Your first program](docs/getting-started.md), then follow the [ten-lesson tutorial](docs/tutorial.md).** Every lesson has a complete source file and expected output. You do not need a library or an account to use the examples.

## Build and try it

The tested platform is Linux x86-64 with GCC. You need a C11 compiler, POSIX threads, `make`, and standard POSIX shell tools. Python is not used. A C compiler is also needed whenever Kremen builds a program.

From the unpacked `kremen` directory:

```sh
make
./build/kremen run examples/tutorial/01_hello.kr
```

You should see:

```text
Hello, Kremen!
```

To install both toolchains in your home directory:

```sh
sh install.sh
export PATH="$HOME/.local/bin:$PATH"
kremen --version
```

The installer builds locally, uses `$HOME/.local` by default, and does not edit your shell settings. For a different location, relocation, or removal, see [Installation](docs/install.md). The source archive contains `install.sh`; it does not contain prebuilt binaries.

## A small program

```go
package main

func doubled(value i64) !i64 {
    if value < 0 {
        return fail(7)
    }
    return ok(value * 2)
}

func main() {
    defer println("finished")
    println(doubled(21) catch 0)
}
```

This prints `42`, then `finished`. `!i64` means the function returns either an integer or an error code. `catch 0` supplies a fallback when it fails. `defer` schedules a call for the end of the current block. The [tutorial](docs/tutorial.md) introduces these ideas one at a time.

## Everyday commands

```sh
./build/kremen check examples/tutorial/04_functions.kr
./build/kremen build examples/tutorial/04_functions.kr -o build/answer
./build/answer
./build/kremen emit-c examples/tutorial/04_functions.kr -o build/answer.c
./build/kremen fmt -w examples/tutorial/04_functions.kr
./build/kremen fmt --check examples/tutorial/04_functions.kr
./build/kremen explain E131
./build/kremen explain --list
./build/kremen check examples/learn/checked_input.kr --mode=learn
```

`check` checks Kremen without invoking a C compiler. `run` builds a temporary executable and runs it. `build` keeps the executable. `emit-c` lets you inspect the generated C, which includes the runtime. `fmt` gives the code its canonical layout. `explain` provides an offline explanation and example for a diagnostic code.

The default C flags for full-language programs are `-std=c11 -Wall -Wextra -Wpedantic -Werror -O3 -pthread`. Safety checks stay enabled at every optimization level. The language also rejects unused bindings and shadowed names. An intentional unused name starts with `_`.

## What changed

Version 0.4.6 fixes unnecessary fixed-array copies in `len`, strengthens result-discard checks in `learn`, and caches repeated zero-initialization checks. Failed artifact writes now use `E015` with a destination and an offline explanation; final stdout failures also produce unsuccessful exits. The checked-input example explains input errors using their own codes. Read the [0.4.6 notes](docs/v0.4.6.md) for behavior, limits, and validation.

[0.4.5](docs/v0.4.5.md) introduced `--mode=work|learn` and source-resolution and diagnostic hardening. Work remains the default; learn adds five requirements while retaining shared safety rules. [0.4.4](docs/v0.4.4.md) introduced offline explanations; the catalog now covers 45 diagnostic categories, including `L001` through `L005`. [0.4.3](docs/v0.4.3.md) introduced independent POSIX workers and JSON diagnostics; the [thread guide](docs/threads.md) explains their boundaries. [Repository update instructions](docs/repository-update.md) explain which files belong in a source update.

The [profile guide](docs/profiles.md) explains the first work/learn interface and its limits. The [roadmap to 0.5](docs/roadmap.md) describes subsequent learning tools. Project profile files, a lesson runner, and browser execution are not implemented yet.

## Where to go next

- [Documentation map](docs/README.md): choose a learning path.
- [Language reference](docs/language.md): syntax, types, built-ins, and limits.
- [Understanding errors](docs/diagnostics.md): read a diagnostic and fix common mistakes.
- [Work and learn profiles](docs/profiles.md): select additional learning requirements.
- [Safety and memory](docs/safety.md): what the compiler checks and what `unsafe` means.
- [Working with threads](docs/threads.md): start independent jobs, collect results, and understand cleanup.
- [Self-hosting](docs/selfhosting.md) and [bootstrap reference](selfhost/README.md).
- [Validation](docs/validation.md), [benchmarks](bench/README.md), and [changelog](CHANGELOG.md).

Kremen is an experimental language. The full frontend still lives in `native/`; the self-hosted subset does not yet implement the full language or its threading API. C tools, libc, POSIX threads, and a host OS remain dependencies. Freestanding kernels, MMIO, shared-memory concurrency primitives, floating point, interfaces, inheritance, and a general C FFI are not implemented. Source and ABI compatibility may change between releases.
