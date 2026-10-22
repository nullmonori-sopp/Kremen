# Historical 0.2 validation

The original 58 native integration checks remain in `tests/native/run.sh`. They cover types, methods, arrays, references, results, slices, modules, formatting, I/O, and output protection. `make test` runs them alongside later regression suites.

Old measurements comparing a Python prototype are historical observations, not current performance results. That implementation and its runner are no longer distributed. Use the native compiler and current shell scripts when reproducing behavior.

The [current validation record](validation.md) describes 0.4.2 checks, sanitizer conditions, installation, and packaging. [Benchmarks](../bench/README.md) explain what the supplied measurements include and exclude.
