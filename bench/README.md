# Measuring Kremen

The scripts here measure specific compiler and runtime workloads. They do not establish that Kremen is faster than another language for arbitrary applications. All runners are native C and shell; the old Python comparison mode is gone.

Saved reports in this directory are **0.4.1 measurements**. They remain historical observations from that build and container, not new 0.4.3 performance claims. Run the scripts again to measure your own checkout.

## Full-frontend checking

```sh
make
sh bench/check.sh
sh bench/check.sh --functions 10000 --runs 10
sh bench/check.sh --compiler ./build/kremen --runs 30
```

The script builds `bench/time.c` and generates its corpus before timing. Requirements are a C11 compiler, shell, `awk`, `cksum`, and `wc`. The default corpus has 2,000 helper functions and a main that calls them: 156,740 bytes, POSIX cksum `364831760 156740`. There is one warm-up followed by 20 separate `check` processes.

Timing includes process startup, source reading, lexing, parsing, semantic checking, and cleanup, plus the runner's fork/exec/wait overhead. It excludes C generation, the external C compiler, linking, and execution of the generated program. The timer uses `CLOCK_MONOTONIC`, `waitpid`, and `getrusage`.

The output records compiler version and checksum, build settings, OS/architecture, corpus parameters, wall time, and CPU time. Means are per batch; the script does not estimate uncertainty or isolate CPU frequency and background load. Use several alternating runs with identical settings when comparing changes.

Saved report: [check-results.txt](check-results.txt).

## Buffer copying

```sh
make bench
make bench USE_ASM=0
```

The copy test first verifies 16,768 length/alignment combinations. It then compares `km_fast_copy` with libc `memcpy` at 16, 64, 1,024, and 65,536 bytes. Regions do not overlap, caches are warm, calls go through function pointers, and CPU time comes from `clock()`.

On Linux x86-64 the compiler uses its small `rep movsb` assembly routine only for copies of at least 64 KiB; smaller copies use libc. The microbenchmark calls the routine directly at every size. `USE_ASM=0` selects a C `memcpy` wrapper. Assembly is an implementation choice, not evidence of a speedup. This test does not measure whole-frontend or compiled-program performance.

## Bootstrap translation

```sh
make selfhost
sh bench/bootstrap.sh --runs 10
```

This measures translation into C, including input, typing, generation, and atomic output, without invoking the C backend. The report records input/compiler checksums and checks the output. Saved report: [bootstrap-results.txt](bootstrap-results.txt).

## Comparing string runtimes

```sh
sh bench/compare-runtime.sh /path/to/kremen-v0.4.0.zip
```

The script extracts the old frontend, its source input, and old runtime. It compiles the same frontend with old and current runtimes, warms both, and runs three alternating pairs of five processes. The generated C must match. This isolates string-runtime changes from the newer emitter's explicit sequencing and checked arithmetic.

Saved report: [runtime-comparison.txt](runtime-comparison.txt). In that 0.4.1 comparison, maximum RSS was 2,560 KiB for the old runtime and 1,792 KiB for the new one in each round. Per-process wall-time means were 0.0224/0.0255/0.0239 seconds versus 0.0248/0.0124/0.0190 seconds. One new-runtime round was slower. The container was not isolated from background work, so these timings do not support a guaranteed percentage speedup.

`maxrss_kib` reports the maximum child RSS, normalized to KiB on Linux/macOS. It is not total allocation volume. Reduced allocation requests do not imply an identical RSS reduction because an allocator can retain pages.

The 0.4.1 C seed grew from roughly 26 KiB to 157 KiB as operations gained explicit sequencing and arithmetic checks. The 0.4.2 seed additionally carries better frontend expression locations. The full compiler's runtime source excerpts also add read-only data. Evaluate these costs with realistic workloads rather than treating one microbenchmark as a language-wide result.

## Compiler changes in 0.4.3

The checker caches computed type layouts, and the full backend interns structural types through a hash table. These changes reduce repeated analysis and lookup work. They do not remove arithmetic or lifetime checks, and there is no release-wide speedup percentage attached to them.

Worker threads introduce their own costs: thread creation, join operations, bookkeeping, and synchronization around shared standard streams. Compare a substantial independent workload both sequentially and concurrently; the small squaring example in the documentation is a teaching example, not a throughput test. Run timing experiments without simultaneous builds or sanitizer jobs, use the same input and compiler options, and report the workload along with its results.
