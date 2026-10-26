# Validation record for 0.4.3

Release environment: Linux x86-64, GCC 14.2.0, POSIX shell. Clang is not installed in this environment. Compiler and generated C use `-std=c11 -Wall -Wextra -Wpedantic -Werror`; full-language generated programs additionally use `-pthread`. Defaults are `-O3`, with `-O2` for the initial bootstrap stages.

The current suites cover 390 checks. They retain the 303 checks from 0.4.2 and add 14 diagnostic checks, 38 checker/codegen checks, 19 thread-language checks, and 16 thread-runtime checks. Negative cases must return the intended diagnostic, not terminate through a signal. Installer checks, bootstrap fixed points, and sanitizer runs are additional validation described below.

## Commands to reproduce

```sh
make test
make test-sanitize
make test-thread-sanitize
make test-install
make examples
```

A clean subset build verifies the committed seed against stages 1, 2, and 3, runs the `720|18|ok` example, and checks four initial malformed-input cases. Driver self-rebuild compares next-stage C for both compiler and command line before publication.

| Suite | Checks | Main coverage |
| --- | ---: | --- |
| `tests/native/run.sh` | 58 | Types, methods, arrays, references, results, slices, imports, formatting, I/O, output preservation |
| `tests/native/v3.sh` | 29 | Defer/catch, arenas, filesystem, enums/matching, tuples |
| `tests/native/frontend_stability.sh` | 13 | Comment boundaries, format behavior/idempotence, token/depth limits, Unicode, malformed input |
| `tests/native/semantic_stability.sh` | 15 | Catch loan boundaries and read-only raw-pointer provenance |
| `tests/native/runtime_stability.sh` | 22 | Arithmetic, file/input capacities, read limits, arenas, LIFO cleanup |
| `tests/native/toolchain_stability.sh` | 16 | Executable discovery, PATH/links, dynamic argv, allocation budgets, CLI, output protection, quoted-path C-only builds |
| `tests/native/diagnostics.sh` | 13 | Underlines, tabs/UTF-8, column 6000+, EOF, related locations, missing files, imported/moved runtime reports, deferred failure, strict standalone C |
| `tests/native/diagnostics_043.sh` | 14 | Source context, JSON validity/ranges/escaping, related locations, import origins, NUL positions, EOF, and CLI rendering |
| `tests/native/checker_codegen_043.sh` | 38 | Preserved effects/checks, host C layouts, structural type reuse, typo hints, thread signatures, handle boundaries, and imported workers |
| `tests/native/threads_043.sh` | 19 | Spawn/join results, deferred joins, worker panic, automatic child cleanup, strict worker types, and forbidden handle escape |
| `tests/native/threads_runtime.sh` | 16 | Actual worker overlap, nested jobs, handle ownership, limits, repeated joins, thread-local resources, panic cleanup, and synchronized input/output |
| `tests/native/tutorial.sh` | 11 | Ten lesson programs, successful/invalid integer input, expected output, and file contents |
| `tests/selfhost/semantics.sh` | 53 | Subset types, scopes, calls, literals, reserved C names, trigraphs |
| `tests/selfhost/toolchain.sh` | 26 | Commands, strict flags, argv/statuses, protected resources, self-rebuild |
| `tests/selfhost/frontend_stability.sh` | 13 | Evaluation order, laziness, loops, checked arithmetic, expression limit, source relocation |
| `tests/selfhost/runtime_stability.sh` | 21 | String chunks/cache, bounds, arithmetic helpers, source aliases, cleanup |
| `tests/selfhost/diagnostics.sh` | 7 | Expression columns, long lines, EOF, invalid token, operand types, undefined names, failed rereads without stale excerpts |
| `tests/native/source_io.sh` | 6 | Nonblocking FIFO rejection in both frontends, formatter, headers, imports, and driver |

The tutorial's file-writing program runs inside a temporary directory. Formatter tests compare behavior before and after formatting, not just text. FIFO tests use `timeout` so a blocked open is a test failure.

## AddressSanitizer and UndefinedBehaviorSanitizer

`make test-sanitize` instruments the native compiler and the subset compiler, driver, runtime, and OS layer. It reruns semantic, frontend, diagnostic, runtime, toolchain, and input checks under ASan/UBSan, including the new checker/codegen and threading regressions. Generated C is also instrumented for selected v0.3, borrowing, bootstrap arithmetic, and seven slice-assignment lifetime scenarios.

`make test-thread-sanitize` runs the 16 C thread-runtime scenarios with ThreadSanitizer. The overlap test uses synchronization to require two workers to be active; success cannot be reproduced by simply executing workers sequentially. Ownership and cleanup tests exercise nested workers, worker-local allocations, and shared standard-stream locking. TSan is an additional host/toolchain check; support for every POSIX system is not implied.

LeakSanitizer is disabled by default because the managed environment does not support it under its tracing setup. This result therefore does not include a leak check. On a supported host, request one with:

```sh
ASAN_OPTIONS=detect_leaks=1 make test-sanitize
```

`UBSAN_OPTIONS=halt_on_error=1` is the default. Arithmetic test failures must name the source operation, rather than producing a sanitizer report about unsafe C arithmetic.

To exercise subset generated behavior without optimization:

```sh
KREMEN_TEST_OPT=-O0 sh tests/selfhost/frontend_stability.sh
```

Native diagnostic fixtures explicitly include both `-O0` and `-O3` builds.

## Installation and portability

`make test-install` builds and installs both compilers into a temporary prefix containing spaces. It checks versions, installed tutorial output and reports, the license and thread guide, runtime lookup, prefix relocation, the installed worker example, subset program execution, upgrade, invalid-manifest refusal, unrelated-file preservation, collision refusal, and uninstall.

The platform suite forces `KM_NO_PROC_SELF_EXE=1` and tests argv/PATH fallback on Linux, including an empty PATH element and executable symlink. A fresh compiler copy is built without assembly from a checkout path containing an apostrophe, using a compiler path with similarly difficult characters. This validates those fallback paths on Linux; it is not evidence of release testing on macOS, Windows, or ARM.

## Source archive checks

```sh
make dist
unzip ../kremen-v0.4.3.zip -d /tmp/kremen-release-check
cd /tmp/kremen-release-check/kremen
sha256sum -c SHA256SUMS
make all selfhost
make test
make test-install
```

The release archives contain source, the installer, documentation, examples, tests, and the committed generated seed. They exclude build directories, executable build products, Python sources, and caches. Validation includes a fresh unpack into a path containing spaces, checksum verification, a clean build, tests, installation/relocation, and subset self-rebuild.

Documentation checks cover local Markdown file links, English-only Markdown prose, and compilable complete examples. The two intentionally invalid programs in the error guide are expected to fail; the other complete examples must pass. Tutorial files are formatted and their outputs are tested.

## What these results do not establish

Passing regressions and sanitizers does not prove the absence of bugs. The full frontend remains C, and the bootstrap remains a smaller language. [Safety and memory](safety.md) defines the current guarantees.

Saved [benchmark reports](../bench/README.md) belong to 0.4.1. They measure selected workloads and are not fresh 0.4.3 performance claims. File-buffer capacities are allocator requests, not promised RSS reductions. The copy microbenchmark separately checks 16,768 size/alignment combinations.
