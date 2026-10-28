# Updating an existing repository

Use the complete 0.4.4 source tree when updating an older project. It includes the preceding stabilization and documentation work. Copying only the new files is not enough: keep compiler sources, build rules, tests, installed resources, and documentation from the same release.

The lists below compare released checksum manifests. Paths are relative to the `kremen` project root. They describe release contents, not any additional local edits in your own repository. Merge those edits before replacing files.

## From 0.4.3 to 0.4.4

Add these files:

```text
bench/compiler-results-0.4.4.txt
bench/compiler.sh
docs/roadmap.md
docs/v0.4.4.md
native/diagnostics.c
tests/native/compiler_core_044.c
tests/native/stabilization_044.sh
```

Replace these existing files:

```text
.github/workflows/ci.yml
CHANGELOG.md
Makefile
README.md
VERSION
bench/README.md
docs/README.md
docs/diagnostics.md
docs/install.md
docs/language.md
docs/repository-update.md
docs/safety.md
docs/v0.2.md
docs/validation.md
native/checker.c
native/core.c
native/kremen.h
native/main.c
native/parser.c
selfhost/toolchain.kr
tests/native/install.sh
tests/native/sanitize.sh
tests/native/toolchain_stability.sh
```

Regenerate and replace `SHA256SUMS` after the source is final. No files are removed. The runtime, `native/codegen.c`, bootstrap compiler source, and verified C seed retain their 0.4.3 contents. Keep the new `native/diagnostics.c` in every custom native-compiler link command; core diagnostics and the CLI both use its offline catalog.

The source installer automatically includes the new Markdown guides and saved benchmark report. Archive names in CI now come from `VERSION`, and CI builds a fresh extraction as well. [0.4.4 notes](v0.4.4.md) explain the behavior; [the roadmap](roadmap.md) describes future profile work separately.

## Historical update to 0.4.3

The 0.4.3 update changes checker, code generation, diagnostics, runtime, build commands, tests, and documentation. New material includes the worker-thread guide, 0.4.3 release notes, worker examples, compiler/runtime regressions, a JSON diagnostic validator, and the GitHub Actions workflow. See [0.4.3 notes](v0.4.3.md) for behavior and compatibility; the old 0.4.2 lists are not a complete 0.4.3 update manifest.

Update both the new files and changed existing files. In particular, keep the matching `native/` sources and `runtime/kremen_rt.h` together: the new worker calls require the new runtime and `-pthread` backend flag. Keep the generated bootstrap seed, its sources, version strings, and installed rebuild resources in sync. The full compiler and self-hosted subset still have different feature sets.

Retain the repository's license. The installer also includes it with installed documentation. Source archives exclude build products; publish those archives as release assets rather than committing them into the source tree. Regenerate `SHA256SUMS` after all source updates are final.

## From the 0.4.2 archive to 0.4.3

These lists compare file contents with the released 0.4.2 checksum manifest. `LICENSE` is new to the source archive; the GitHub repository already had it, and its text is preserved.

### Add these files

```text
.github/workflows/ci.yml
LICENSE
docs/threads.md
docs/v0.4.3.md
examples/threads.kr
tests/native/checker_codegen_043.sh
tests/native/diagnostics_043.sh
tests/native/json_validate.c
tests/native/threads_043.sh
tests/native/threads_runtime.c
tests/native/threads_runtime.sh
```

### Replace these existing files

```text
CHANGELOG.md
Makefile
README.md
VERSION
bench/README.md
docs/README.md
docs/diagnostics.md
docs/install.md
docs/language.md
docs/repository-update.md
docs/safety.md
docs/selfhosting.md
docs/tutorial.md
docs/v0.2.md
docs/v0.4.1.md
docs/v0.4.md
docs/validation-v0.2.md
docs/validation.md
install.sh
native/checker.c
native/codegen.c
native/core.c
native/kremen.h
native/main.c
runtime/kremen_rt.h
selfhost/README.md
selfhost/toolchain.kr
tests/native/diagnostics.sh
tests/native/install.sh
tests/native/run.sh
tests/native/runtime_stability.sh
tests/native/sanitize.sh
tests/native/semantic_stability.sh
tests/native/toolchain_stability.sh
tests/native/v3.sh
tools/package.sh
```

Replace `SHA256SUMS` with the regenerated manifest as well. Keep executable permissions on `install.sh`, `kremenc`, `selfhost/bootstrap.sh`, and `tools/package.sh`. No source files are removed. In particular, `selfhost/bootstrap.kr` and `selfhost/stage0.c` retain their verified contents; the subset driver's version changes in `selfhost/toolchain.kr`.

## Historical changes in 0.4.1

0.4.1 fixed borrowing through `catch`, enforced shared-pointer read-only provenance, preserved comments around `else`, and bounded compiler allocation. It corrected bootstrap evaluation order and arithmetic, reduced string and backend allocation overhead, improved executable/path discovery, and tightened output protection and CLI validation. It added the platform header and stability suites listed below. The [0.4.1 release notes](v0.4.1.md) explain behavior and compatibility.

## Historical changes in 0.4.2

0.4.2 adds English learning material, ten runnable lesson programs, and source-aware diagnostics. It updates the native compiler, runtime, bootstrap frontend and generated seed, installer, tests, version strings, and every existing Markdown page. The [0.4.2 notes](v0.4.2.md) explain these changes.

## From 0.4.1 to 0.4.2

### Add these files

```text
docs/README.md
docs/diagnostics.md
docs/getting-started.md
docs/repository-update.md
docs/tutorial.md
docs/v0.4.2.md
examples/tutorial/01_hello.kr
examples/tutorial/02_variables.kr
examples/tutorial/03_control_flow.kr
examples/tutorial/04_functions.kr
examples/tutorial/05_records_arrays.kr
examples/tutorial/06_input.kr
examples/tutorial/07_results.kr
examples/tutorial/08_enums_tuples.kr
examples/tutorial/09_memory.kr
examples/tutorial/10_files_cleanup.kr
tests/native/diagnostics.sh
tests/native/tutorial.sh
tests/selfhost/diagnostics.sh
```

### Replace these existing files

```text
CHANGELOG.md
Makefile
README.md
VERSION
bench/README.md
docs/install.md
docs/language.md
docs/safety.md
docs/selfhosting.md
docs/v0.2.md
docs/v0.3.md
docs/v0.4.1.md
docs/v0.4.md
docs/validation-v0.2.md
docs/validation.md
examples/demo.kr
examples/hello.kr
examples/io.kr
examples/references.kr
examples/unsafe.kr
install.sh
native/checker.c
native/codegen.c
native/core.c
native/kremen.h
native/main.c
native/modules.c
runtime/kremen_rt.h
selfhost/README.md
selfhost/bootstrap.kr
selfhost/runtime.c
selfhost/stage0.c
selfhost/toolchain.kr
tests/native/install.sh
tests/native/run.sh
tests/native/sanitize.sh
tests/selfhost/sanitize.sh
```

No source files are removed by this patch.

## From 0.4.0 to 0.4.1

These historical lists describe the previous update. If you are installing 0.4.2 directly, use the current files in the complete archive; do not copy older replacements over them.

### Add these files

```text
bench/bootstrap-results.txt
bench/bootstrap.sh
bench/compare-runtime.sh
bench/runtime-comparison.txt
docs/v0.4.1.md
selfhost/platform.h
tests/native/frontend_stability.sh
tests/native/platform_stability.c
tests/native/runtime_stability.c
tests/native/runtime_stability.sh
tests/native/semantic_stability.sh
tests/native/semantic_values.kr
tests/native/toolchain_stability.sh
tests/selfhost/frontend_stability.sh
tests/selfhost/runtime_stability.c
tests/selfhost/runtime_stability.sh
```

### Replace these existing files

```text
CHANGELOG.md
Makefile
README.md
VERSION
bench/README.md
bench/check-results.txt
bench/check.sh
bench/time.c
docs/install.md
docs/language.md
docs/safety.md
docs/selfhosting.md
docs/v0.4.md
docs/validation-v0.2.md
docs/validation.md
install.sh
native/checker.c
native/codegen.c
native/core.c
native/format.c
native/kremen.h
native/lexer.c
native/main.c
native/parser.c
runtime/kremen_rt.h
selfhost/README.md
selfhost/bootstrap.kr
selfhost/host.c
selfhost/runtime.c
selfhost/runtime.h
selfhost/stage0.c
selfhost/toolchain.kr
tests/native/run.sh
tests/native/sanitize.sh
tests/selfhost/sanitize.sh
tests/selfhost/toolchain.sh
```

No source files were removed between 0.4.0 and 0.4.1.

## Generated files that do belong in the repository

Keep `selfhost/stage0.c`. It is generated, but it is the checked-in bootstrap seed required to build without an existing Kremen compiler. It must match `bootstrap.kr`; do not hand-edit it. Keep the saved benchmark reports with their version context rather than relabeling old timings as new measurements.

`SHA256SUMS` is release metadata, generated by `make dist` after the source is final. Replace the old manifest with the new one. It lists every packaged file except itself and is intentionally not included in the hash-based file lists above. If you change source after packaging, regenerate the package and manifest.

Do not commit `build/`, `selfhost/build/`, temporary generated C, executables, test output, or local logs. Publish the versioned ZIP, tar.gz, and external `.sha256` as release downloads; the project's ignore rules already exclude the archives. There is no Python source or dependency to add.

## Check and stage the update

From your repository's project root:

```sh
make test
make test-sanitize
make test-thread-sanitize
make test-install
make dist
git add .github .gitignore LICENSE README.md CHANGELOG.md Makefile VERSION install.sh kremenc
git add native runtime selfhost examples tests docs bench tools SHA256SUMS
git diff --cached --stat
git diff --cached
```

The `git` commands are instructions for your repository; creating an archive does not itself make a commit or push anything. Review the staged files alongside your own changes before committing. The source distribution has everything needed to rebuild both toolchains and their tests without checking in build products.
