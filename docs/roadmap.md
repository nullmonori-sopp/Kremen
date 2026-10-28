# From 0.4.4 to 0.5

The goal is to make Kremen useful for ordinary systems experiments and for structured learning. The planned `work` and `learn` profiles will share language semantics, safety rules, module formats, and the C backend. Learning adds explicit requirements and explanations; it will not make invalid memory accesses acceptable in another profile.

This is a development plan. Each release must be tested on its own merits, and unfinished work can move to a later milestone. Only the 0.4.4 row below describes work available in this release.

| Version | Focus | Evidence needed before calling it complete |
| --- | --- | --- |
| 0.4.4 | Offline explanations, semantic and source-range fixes, compiler formatting work | All native codes have explanations; inference and range regressions pass; normal, sanitizer, installation, and archive checks pass |
| 0.4.5 | A more consistent diagnostic model and editor-facing data | Stable code/range tests, clear component boundaries, and safe handling of independent errors without misleading cascades |
| 0.4.6 | Introduce typed intermediate operations incrementally | Preserve evaluation order, panic and cleanup paths, bounds checks, and generated behavior through each lowering change |
| 0.4.7 | Resource and function-effect groundwork | Specify ownership and effect rules first; check any implemented contracts across calls, including conservative treatment of unknown external effects |
| 0.4.8 | A small opt-in `work`/`learn` profile pilot | A documented rule set, tested CLI precedence, identical behavior for accepted source, and useful feedback for every extra learning requirement |
| 0.4.9 | Deterministic lesson runner and a first learning track | Runnable lessons, observable tests, ordered hints, offline operation, and validation by people unfamiliar with the language |
| 0.4.10 | Stabilize the release candidate | Compatibility review, bounded diagnostics, regression coverage, clean installations, complete English guides, and no placeholder commands |
| 0.5.0 | Publish the tested profile and learning workflow | A coherent first course and a stable documented interface; remaining experimental features clearly identified |

The release after 0.4.10 is planned as 0.5.0, rather than 0.4.11. These are version numbers: 0.4.10 comes after 0.4.9.

## What exists today

0.4.4 has one strict full-language compiler profile, an offline diagnostic catalog, ten tutorial programs, and independent integer workers. `kremen explain CODE` and `kremen explain --list` work now. Proposed `--mode=learn`, project profile configuration, `kremen learn`, and browser execution do not exist yet.

The first learning requirements should teach real properties: explicit failure handling, resource cleanup, checked ranges, and understanding allocation or blocking. A rule needs a precise definition and a useful remedy. Broad claims about program intent, absence of all deadlocks, or guaranteed execution time require evidence the compiler does not currently provide.

Requirements should increase through lessons. A first-language learner needs small tasks and accessible explanations before pointer lifetimes, concurrent shutdown, or memory-ordering exercises. A completed task should explain how its idea transfers to another language.

## Work that needs a separate design

General shared-memory concurrency requires a transfer and sharing model before slices or pointers can cross threads. Generics, typed application errors, automatic resource destruction, C ABI control, and freestanding targets also need specifications and focused tests. They are not promised as incidental additions to these patch releases.

Full self-hosting means a Kremen-written parser, checker, and backend for the full language, with reproducible bootstrap stages. The current subset compiler and driver do not meet that goal. Removing the external C toolchain is a further backend project. A website or application can reuse a future lesson engine; running the compiler or programs in a browser requires additional WebAssembly or server work.
