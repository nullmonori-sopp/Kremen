# The Kremen bootstrap toolchain

This directory contains a compiler and command-line driver written in the subset of Kremen that they compile. Use the full compiler in `native/` for ordinary Kremen programs, including the tutorial. See [Self-hosting and systems work](../docs/selfhosting.md) for user commands and the larger roadmap.

## Source map

| File | Responsibility |
| --- | --- |
| `bootstrap.kr` | Lexer, parser, semantic checks, and C generation |
| `toolchain.kr` | Command-line parsing, build/run policy, and next-stage verification |
| `stage0.c` | Committed generated C seed for `bootstrap.kr` |
| `runtime.c`, `runtime.h` | Strings, checked arithmetic, source input/output, arguments, and arena storage |
| `host.c`, `host.h` | Process, path, temporary-file, and artifact-publication primitives |
| `platform.h` | Executable discovery, including the POSIX argv/PATH fallback |
| `driver.c` | Small C entry point calling `kr_main` |
| `bootstrap.sh` | Initial build and fixed-point checks |

There is no parser or generator hidden in the C host layer. C provides the operations the subset cannot yet implement. No Python or network dependency is involved.

## Reproduce the compiler

```sh
make selfhost
selfhost/build/kremen-selfhost check selfhost/example.kr
selfhost/build/kremen-selfhost run selfhost/example.kr -- /tmp/example.txt
cat /tmp/example.txt
selfhost/build/kremen-selfhost bootstrap -o build/kremen-selfhost-next
```

The example produces `720|18|ok`. Initial stages use `-std=c11 -O2 -Wall -Wextra -Wpedantic -Werror`; the user-facing driver defaults to `-O3`.

`bootstrap.sh` compiles the seed, translates and compiles stage 1, translates and compiles stage 2, and obtains stage 3. It checks `stage0.c == stage1.c == stage2.c == stage3.c`. The driver also verifies C for both the compiler and CLI during `bootstrap`. Comparisons concern C source, not machine code.

## The subset's language

The types are `i64`, `bool`, and `string`. Variables and parameters have explicit types. Functions require a result type; the entry point is `func main() i64`. Supported declarations include local/global `var`, functions, and `extern func`. Functions may call one another before definition because signatures are collected first.

Statements include assignment, function calls, `return`, `if`/`else if`/`else`, `for condition`, `break`, `continue`, and an `unsafe` block marker. Arithmetic, comparisons, boolean operators, decimal integers, and strings are supported. Types, calls, scopes, argument counts, and return paths are checked before C generation. There is no implicit `bool`/integer conversion. Compare strings with `rt_equal`, not `==`.

Comments use `//`. String escapes include `\n`, `\r`, `\t`, `\\`, and `\"`; strings are immutable NUL-terminated C strings, so embedded NUL is unsupported. C keywords, leading-underscore names, and C-header/backend name families are reserved. Call results may be discarded as statements; the full language's unused-binding rules do not all apply here.

Function-body operands and arguments are explicitly sequenced left to right. `&&` and `||` remain lazy. Loop conditions are recalculated on every iteration, including after `continue`. Integer helpers check overflow, division/remainder by zero, and the minimum-integer divided by minus-one case. Global initializers remain constant C expressions checked by the strict backend.

The subset does not implement imports, structs, enums, methods, tuples, arrays, references, pointers, slices, `!T`, `defer`, `catch`, or `arenablock`. `unsafe` does not activate a borrow checker. The full compiler's memory guarantees must not be assumed here.

Limits are 8 MiB per input, parser depth 128, 512 operations per expression chain, and a 512 MiB string arena. Small strings use 64 KiB chunks; large strings use dedicated chunks. Known string lengths use a bounded cache without reading before arbitrary C string pointers.

## Diagnostics

Frontend messages contain the input path, line, column, and a bounded excerpt. In 0.4.2, type mismatches point at the source expression rather than the following token; undefined names and calls point at the name. The excerpt comes from the input already read into memory.

Generated arithmetic errors use an embedded source basename and line/column. Basenames keep generated C reproducible across checkout relocation. Those subset runtime errors do not embed the full compiler's source excerpts. Backend errors name generated C files.

## Updating the seed

Keep a working previous stage before changing `bootstrap.kr`. Generate a candidate with that previous compiler, verify it can reproduce itself, then replace the committed seed. Never hand-edit `stage0.c`.

From an existing working build, the shortest workflow is:

```sh
selfhost/build/stage2 selfhost/bootstrap.kr build/candidate-seed.c
cp build/candidate-seed.c selfhost/stage0.c
sh selfhost/bootstrap.sh
make test-selfhost
```

Only commit the seed after the fixed point and tests pass. If the new compiler needs syntax the previous stage does not support, introduce that feature in smaller steps. Test the driver as well as the frontend: both use this parser. The 0.4.2 seed includes expression-location tracking and was regenerated through the previous stage.
