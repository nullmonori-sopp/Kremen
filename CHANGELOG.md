# Changelog

## 0.4.5 — 2026-10-28

- Add full-language `--mode=work|learn`, with compatible work defaults and five learning requirements checked after shared semantics. Keep accepted source's C output identical between profiles.
- Add `L001`–`L005` explanations, a checked-input example, profile-aware JSON diagnostics, and cached checks for discarded result-bearing aggregates.
- Fix exponential traversal of checked method-call receivers during backend type scanning and defer-callback discovery.
- Apply canonical project-root checks to directory sources, automatic headers, and entry modules; validate that `--root` names a directory.
- Escape terminal controls, bidi directives, and invalid bytes in frontend diagnostic text and runtime source paths. Bound text messages, preserve JSON byte information, and locate `catch` at its operator.
- Add security regressions, dual-profile checks, and a fixed-seed mutation corpus. Extend installation, sanitizers, and English guides. See [release notes](docs/v0.4.5.md).

## 0.4.4 — 2026-10-28

- Add offline `kremen explain CODE` and a list of all 39 native diagnostic categories, with reasons, remedies, and examples.
- Link text and JSON errors to their explanation command; report more CLI, formatter, and protected-output failures through structured diagnostics.
- Infer integer context from imported functions, methods, borrowed receivers, and nested method calls without executing or borrowing them during the hint pass.
- Underline the actual unknown type, include the full qualified type range, and search imported type names for spelling suggestions.
- Format short compiler strings once, retaining the long-string fallback; add boundary and variadic-argument regressions and a reproducible emitter comparison.
- Extend installer, clean-archive CI, documentation, and regressions. Record the staged [roadmap to 0.5](docs/roadmap.md); learning profiles remain future work. See [release notes](docs/v0.4.4.md).

## 0.4.3 — 2026-10-26

- Add native POSIX workers: `threadSpawn`, `threadJoin`, and deferred `threadClose`, with copied `i64` input/output and creator-owned handles.
- Keep defer stacks and runtime resource bookkeeping thread-local; join outstanding children at creator exit and report worker panic through `ECANCELED`.
- Reject unsafe worker signatures, captured/shared addresses, returned handles, and handles inside ordinary aggregates.
- Preserve side effects and bounds checks in unary expressions involving constant array lengths.
- Correct C-compatible result and slice layouts near the value-size limit; cache layouts and use hashed structural type lookup in the backend.
- Add surrounding source context, related-location underlines, import origins, typo suggestions, exact NUL-byte locations, and JSON frontend diagnostics.
- Extend regression coverage, release documentation, source packaging, and continuous integration. See [release notes](docs/v0.4.3.md).

## 0.4.2 — 2026-10-22

- Rewrite all Markdown documentation in English; add first-program instructions, ten guided lessons, complete runnable examples, an error guide, and a documentation map.
- Add numbered, bounded source excerpts, token underlines, and help text to native diagnostics; fix clipping beyond the old long-line window.
- Report earlier borrow and local declaration locations, and identify missing source paths consistently.
- Embed full-language runtime locations and excerpts, including imported files and deferred operations, without lookup on successful checks.
- Track source expressions and names in bootstrap type/name errors; add input excerpts and regenerate the verified C seed.
- Install saved benchmark reports, verify installed example output, and use English output in general examples.
- Add diagnostic and tutorial regressions, and document exact repository additions/replacements for this and the preceding release.

## 0.4.1 — 2026-10-22

- Preserve outer expression loans through `catch`; reject writes through raw pointers derived from shared references.
- Preserve block comments around `if`/`else`; bound tokens and compiler arena allocation with checked size arithmetic.
- Sequence bootstrap expressions and check arithmetic; regenerate the reproducible seed.
- Pack bootstrap strings into chunks, cache lengths and backend type mappings, use stack formatting, and shrink successful input/file strings where possible.
- Add executable lookup without `/proc`, C fallback outside Linux x86-64, support quoted paths, and grow child argument storage dynamically.
- Protect sources/runtime/compiler from overwrite; validate CLI options and support `--`.
- Extend regressions, sanitizer coverage, benchmarks, installer resources, and source packaging. See [release notes](docs/v0.4.1.md).

## 0.4.0 — 2026-10-21

- Add the Kremen-written subset CLI: `check`, `emit-c`, `build`, `run`, and `bootstrap`, reachable through `kremen selfhost`.
- Check subset names, scopes, calls, operators, conditions, returns, literals, and entry points before the C backend.
- Verify compiler and CLI next-stage C during self-rebuild.
- Launch children without a shell, preserve arguments/statuses, and publish artifacts atomically after success.
- Protect sources and resources through path/link aliases; reject FIFO inputs without waiting.
- Fix C-header identifier conflicts and C11 string trigraph preservation.
- Install subset rebuild resources and extend relocation, removal, and sanitizer checks.

## 0.3.0 — 2026-10-20

- Add `defer`, result `catch` and matching, arenas, filesystem built-ins, payload enums, exhaustive `match`, tuples, and destructuring.
- Extend formatting, regression coverage, and ASan/UBSan checks.
- Remove the Python implementation and tests from the source package.
- Add an installer, executable-relative runtime lookup, source archives, and checksum manifests.

## 0.2

Introduce the native compiler, strict C11 flags, local imports and headers, results, slices, formatting, and a subset bootstrap. See [migration notes](docs/v0.2.md).

## 0.1

Initial syntax, types, structs, methods, and reference checks. The original implementation is no longer distributed.
