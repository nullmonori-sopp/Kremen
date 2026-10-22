# Kremen 0.4.2

Kremen is a small compiled language for learning and experimenting with systems programming. Its syntax is inspired by Go. It has explicit integer types, structs and methods, checked references, raw pointers, arrays, slices, results, and scoped resource cleanup.

The main compiler is written in C11. It translates Kremen into C and asks a C compiler to produce an executable. A separate, smaller compiler and command-line driver are written in Kremen and can rebuild themselves. The [self-hosting guide](docs/selfhosting.md) explains that distinction.

**New to programming? Start with [Your first program](docs/getting-started.md), then follow the [ten-lesson tutorial](docs/tutorial.md).** Every lesson has a complete source file and expected output. You do not need a library or an account to use the examples.

## Build and try it

The tested platform is Linux x86-64 with GCC. You need a C11 compiler, `make`, and standard POSIX shell tools. Python is not used. A C compiler is also needed whenever Kremen builds a program.

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
```

`check` checks Kremen without invoking a C compiler. `run` builds a temporary executable and runs it. `build` keeps the executable. `emit-c` lets you inspect the generated C, which includes the runtime. `fmt` gives the code its canonical layout.

The default C flags are `-std=c11 -Wall -Wextra -Wpedantic -Werror -O3`. Safety checks stay enabled at every optimization level. The language also rejects unused bindings and shadowed names. An intentional unused name starts with `_`.

## What changed

Version 0.4.2 adds English learning material and source excerpts in compiler and runtime errors. It fixes diagnostic clipping on long lines and identifies the origin of conflicting borrows and local declarations. Runtime locations survive moving an executable without its source files. See [0.4.2 notes](docs/v0.4.2.md) for costs and limits.

The previous release, 0.4.1, fixed borrowing through `catch`, read-only raw pointers, formatting around `else`, and bootstrap evaluation order. It also reduced temporary allocation and improved toolchain path handling. The [0.4.1 notes](docs/v0.4.1.md) explain the changes. [Repository update instructions](docs/repository-update.md) list the exact added and replaced files for both releases.

## Where to go next

- [Documentation map](docs/README.md): choose a learning path.
- [Language reference](docs/language.md): syntax, types, built-ins, and limits.
- [Understanding errors](docs/diagnostics.md): read a diagnostic and fix common mistakes.
- [Safety and memory](docs/safety.md): what the compiler checks and what `unsafe` means.
- [Self-hosting](docs/selfhosting.md) and [bootstrap reference](selfhost/README.md).
- [Validation](docs/validation.md), [benchmarks](bench/README.md), and [changelog](CHANGELOG.md).

Kremen is an experimental language. The full frontend still lives in `native/`; the self-hosted subset does not yet implement the full language. C tools, libc, and a host OS remain dependencies. Freestanding kernels, MMIO, threads, floating point, interfaces, inheritance, and a general C FFI are not implemented. Source and ABI compatibility may change between releases.
