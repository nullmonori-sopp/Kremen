# Validation record for 0.4.8

The local release environment is Linux x86-64, GCC 14.2.0, and a POSIX shell. Native Windows validation is a separate PowerShell/Clang job. Compiler and generated C use strict C11 warnings as errors. POSIX full-language programs add `-pthread`; the Windows adapter uses native thread primitives without that compatibility library. Defaults are `-O3`, with `-O2` for the initial bootstrap stages. Portable-package checks use the package's real bundled SDK rather than the machine's development compiler.

The Linux suites contain the 900 checks from 0.4.7 plus 18 native-runtime, 9 backend/package, and 6 subset-output cases, for 933 checks. Negative cases must return the intended diagnostic, not terminate through a signal. Installer checks, bootstrap fixed points, sanitizer runs, native Windows checks, and real portable-package execution are additional validation described below. Full-device tests use Linux `/dev/full` and explicitly skip those cases when it is unavailable, so other hosts can report a smaller count. A Windows integration count is separate from the Linux count.

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
| `tests/native/stabilization_044.sh` | 29 | Offline catalog coverage, structured command/formatter/output errors, exact type ranges, imported/nested/borrowed method inference, and formatting boundaries |
| `tests/native/profiles_045.sh` | 41 | All five learning rules, aggregate result discards, common borrowing/runtime checks, profile precedence, byte-identical C, error handling, and output preservation |
| `tests/native/security_045.sh` | 25 | Long method/defer chains, canonical source/header/entry boundaries, symlinks, control-safe paths/messages/excerpts, bounded text, and runtime cleanup |
| `tests/native/malformed_045.sh` | 256 | Fixed-seed mutated sources in both profiles; bounded termination with valid JSON errors or strict-C-valid output |
| `tests/native/checker_046.sh` | 23 | Result-bearing patterns, parameters, receivers, imported bodies, preserved zero-state rules, and bounded shared-type checking |
| `tests/native/codegen_046.sh` | 34 | Fixed-array length without extra copies; 8 MiB stack, both optimization levels and profiles, staged calls/defer, preserved bounds/lifetime/result/conversion failures |
| `tests/native/input_046.sh` | 16 | Learning example in both profiles: positive/nonpositive input, EOF, format, positive/negative range errors, and closed stdin |
| `tests/native/output_046.sh` | 14 | Destination-aware text/JSON failures, blocked paths, forced staged-write failures, output/source preservation, cleanup, permissions, and compiler stdout errors |
| `tests/native/output_runtime_046.sh` | 8 | Normal/full stdout for main/worker output and panic, failure exit status, worker joining, and deferred marker-file cleanup |
| `tests/native/semantic_047.sh` | 38 | Large aggregate discards and entirely ignored tuple patterns, 8 MiB stack, both optimization levels and profiles, effects, partial bindings, preserved runtime failures and learn rules |
| `tests/native/runtime_047.sh` | 12 | Binary file data and modes, file replacement and same-identity rename, filesystem errors, metadata, and sticky stream-error handling |
| `tests/native/doctor_047.sh` | 14 | Actual strict-C/pthread worker probe, backend selection and failures, literal process arguments, CLI validation, offline explanation, and relocated runtime lookup |
| `tests/native/runtime_048.sh` | 18 | Unicode files/directories, invalid paths, native workers, nested joining/cleanup, ownership failures, automatic joins, and handled worker failure |
| `tests/native/backend_048.sh` | 9 | Bundled backend argument transport, executable discovery and package construction using a controlled fixture SDK |
| `tests/native/threads_043.sh` | 19 | Spawn/join results, deferred joins, worker panic, automatic child cleanup, strict worker types, and forbidden handle escape |
| `tests/native/threads_runtime.sh` | 16 | Actual worker overlap, nested jobs, handle ownership, limits, repeated joins, thread-local resources, panic cleanup, and synchronized input/output |
| `tests/native/tutorial.sh` | 11 | Ten lesson programs, successful/invalid integer input, expected output, and file contents |
| `tests/selfhost/semantics.sh` | 53 | Subset types, scopes, calls, literals, reserved C names, trigraphs |
| `tests/selfhost/toolchain.sh` | 26 | Commands, strict flags, argv/statuses, protected resources, self-rebuild |
| `tests/selfhost/frontend_stability.sh` | 13 | Evaluation order, laziness, loops, checked arithmetic, expression limit, source relocation |
| `tests/selfhost/runtime_stability.sh` | 21 | String chunks/cache, bounds, arithmetic helpers, source aliases, cleanup |
| `tests/selfhost/diagnostics.sh` | 7 | Expression columns, long lines, EOF, invalid token, operand types, undefined names, failed rereads without stale excerpts |
| `tests/selfhost/output_048.sh` | 6 | Subset driver and generated-program output failures, unsuccessful exit status, and retained successful output |
| `tests/native/source_io.sh` | 6 | Nonblocking FIFO rejection in both frontends, formatter, headers, imports, and driver |

The tutorial's file-writing program runs inside a temporary directory. Formatter tests compare behavior before and after formatting, not just text. FIFO tests use `timeout` so a blocked open is a test failure.

The mutation generator uses seed `0x4b524d45`, eight small source templates, and byte insertions, replacements, deletions, or truncations to create 128 inputs. Original seeds and mutations run in work and learn. Accepted cases must pass strict C syntax checking; rejected cases must emit valid JSON carrying the active mode. A ten-second per-command deadline catches runaway work. Generated mutants are syntax-checked rather than executed. The corpus is reproducible and targeted; it does not replace coverage-guided fuzzing or establish exhaustive input coverage.

The new codegen suite compiles fixtures at `-O0` and `-O3`. Its repeated large-array length case runs with an 8 MiB stack; the checker-only shared-type case uses 6,000 locals under a deadline and is never executed. Output tests use a file-size limit to fail staged writes without relying on the user's permission level. They verify that the previous artifact or source remains, that temporary paths are removed, and that errors remain valid JSON when requested. Runtime-output cases use deferred file writes to prove cleanup completed even when stdout is a full device.

0.4.7 extends the stack regressions to explicit aggregate discards and entirely ignored tuple patterns. It also checks partially used tuple patterns, work/learn acceptance, source calls and indexing order, and retained lifetime, bounds, result, and integer-conversion failures. Runtime fixtures retain CRLF, NUL, and `0x1a` bytes and cover a sticky file-stream error followed by an empty write. Doctor tests perform a real compile/run setup probe; that probe is intentionally smaller than the regression suites.

The 0.4.8 runtime fixture uses the same C cases on Linux and Windows. It checks valid Unicode names, rejected invalid path data, native and nested worker execution, retained creator ownership, repeated joins, automatic cleanup, and a handled child failure. The backend fixture records literal arguments and builds a controlled package tree. It does not pretend that its fixture SDK compiled a program; the real-SDK checks below provide that evidence.

## AddressSanitizer and UndefinedBehaviorSanitizer

`make test-sanitize` instruments the native compiler and the subset compiler, driver, runtime, and OS layer. It reruns semantic, frontend, diagnostic, runtime, toolchain, and input checks under ASan/UBSan, including the profile, security, mutation, stabilization, checker/codegen, native-output, runtime-output, doctor, and threading regressions. The native-runtime and backend regressions participate too. Diagnostic and formatting helpers, the learning example, fixed-array length/discard fixtures, long chains, and imported/method fixtures are instrumented too. Generated C is also instrumented for selected v0.3, borrowing, bootstrap arithmetic, and seven slice-assignment lifetime scenarios. Mutated accepted programs receive strict C syntax checks; the generator itself is ordinary native C. Doctor's native compiler is instrumented in this run; the setup probe itself uses its ordinary strict backend flags.

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

`make test-install` builds and installs both compilers into a temporary prefix containing spaces. It checks versions, offline native/learning explanations, the installed learn-profile example before and after relocation, installed tutorial output and reports, the license and thread guide, runtime lookup, prefix relocation, the installed worker example, subset program execution, upgrade, invalid-manifest refusal, unrelated-file preservation, collision refusal, and uninstall.

The platform suite forces `KM_NO_PROC_SELF_EXE=1` and tests argv/PATH fallback on Linux, including an empty PATH element and executable symlink. A fresh compiler copy is built without assembly from a checkout path containing an apostrophe, using a compiler path with similarly difficult characters. This validates those fallback paths on Linux; it is not evidence of release testing on macOS or ARM.

## Native Windows profile

Windows checks run as native x86-64 programs from PowerShell 7, using Clang and the Windows SDK for source builds:

```powershell
.\build.ps1 -Compiler clang -Selfhost
.\tests\windows\native.ps1 -Kremen .\build\kremen.exe -Compiler clang
.\tests\windows\selfhost.ps1 -Toolchain .\selfhost\build\kremen-selfhost.exe -Compiler clang
```

The frontend checks exercise native process arguments, both semantic profiles, Unicode sources and temporary paths, formatting, imports, deep module chains, JSON errors, source protection, files, and workers. The subset checks cover native bootstrap, Unicode paths, literal program arguments, executable names, and verified self-rebuild. The direct C runtime fixture covers the same 18 cases as its Linux runner. The historical MSYS2 script is not the 0.4.8 native Windows validation target.

Cross-compilation can catch declarations and linker problems, but it does not prove native execution. Likewise, doctor only proves that its small runtime program compiled and ran. Native Windows execution, Linux regressions, and compiler/sanitizer checks are separate evidence. The release targets Windows x86-64 and Linux x86-64; it does not establish every old Windows version, every Linux host, other architectures, every Windows path namespace, or a separate MSVC frontend build policy.

## Real portable packages

The portable checks unpack the complete SDK-backed packages, clear `CC`, remove development compilers from `PATH`, and compile and execute Kremen programs through the discovered bundled backend. They check both work and learn, Unicode paths, relocation, runtime lookup, and subset use. The compiler, launcher, resources, SDK libraries, and license files move together.

The Linux portable release links its frontend and subset driver statically. Its backend uses `x86_64-linux-musl` and static linking for generated programs. This avoids the host's glibc ABI; the package still requires a compatible Linux kernel and x86-64 CPU. A source build with a system compiler keeps that compiler's normal runtime choices.

Package creation downloads a selected SDK only when explicitly requested. It checks that version's archive checksum before extraction; ordinary use after unpacking is offline. Checksums detect changed bytes and are not publisher signatures. These checks do not establish reproducibility of the third-party SDK's own executable build.

## Source archive checks

```sh
make dist
unzip ../kremen-v0.4.8.zip -d /tmp/kremen-release-check
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

The checking, bootstrap, and string-runtime [benchmark reports](../bench/README.md) belong to 0.4.1. The separate 0.4.4 emitter comparison checks identical C output against 0.4.3 and records variable timings; it does not establish a guaranteed speedup. File-buffer capacities are allocator requests, not promised RSS reductions. The copy microbenchmark separately checks 16,768 size/alignment combinations.
