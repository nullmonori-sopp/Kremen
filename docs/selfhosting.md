# Self-hosting and systems work

A compiler is **self-hosted** when it can compile the language implementation written in its own language. Kremen has a self-hosted compiler and CLI for a small bootstrap subset. The compiler for the full language is still written in C. These are two different frontends, and keeping that boundary clear matters when choosing which one to use.

The bootstrap frontend and its build policy live in `.kr` files. C supplies strings, memory, filesystem access, process primitives, and an entry point. A C11 toolchain, libc, and a hosted OS are still required. Self-hosting has not removed those dependencies.

## Build and use the subset toolchain

From the source root:

```sh
make all selfhost
./build/kremen selfhost --help
./build/kremen selfhost check selfhost/example.kr
./build/kremen selfhost build selfhost/example.kr -o build/bootstrap-example
./build/kremen selfhost run selfhost/example.kr -- /tmp/kremen-example.txt
cat /tmp/kremen-example.txt
./build/kremen selfhost emit-c selfhost/example.kr -o build/bootstrap-example.c
./build/kremen selfhost bootstrap -o build/kremen-selfhost-next
```

The example writes `720|18|ok` to the output path passed as its argument. The separate executable `selfhost/build/kremen-selfhost` accepts the same commands without the `selfhost` prefix. After installation, use `kremen selfhost ...` or `kremen-selfhost ...`.

| Command | Work performed |
| --- | --- |
| `check SOURCE.kr` | Subset parsing and type checking, followed by strict C syntax checking |
| `emit-c SOURCE.kr -o FILE` | Generate C into a required explicit output file |
| `build SOURCE.kr [-o FILE]` | Build an executable, default `a.out` |
| `run SOURCE.kr [-- ARG ...]` | Build temporarily and run with the supplied arguments |
| `bootstrap [-o FILE]` | Rebuild the subset compiler and CLI, then verify the next stage; default `kremen-selfhost` |

`--cc EXE` and `-O0` through `-O3` select the C compiler and optimization. The default is `CC` or `cc`, with `-O3`, plus `-std=c11 -Wall -Wextra -Wpedantic -Werror`. The compiler setting names an executable, not a shell expression.

Arguments after `--` go literally to the program, including spaces and shell metacharacters. The driver uses separate `argv` entries and no shell. It preserves the program's exit status, using `128 + signal` when a child is terminated by a signal. The full-language `run` command does not yet expose the same program-argument interface.

## What a fixed point proves

`bootstrap` generates C for both `bootstrap.kr` and `toolchain.kr`, builds the next driver, then asks it to generate C for both sources again. The files must match byte for byte before the executable is published.

The initial build also checks the committed C seed against stages 1, 2, and 3. This is a fixed point of generated C source, not a promise of byte-identical executable files or a proof that the compiler has no bugs. It demonstrates that the subset implementation can reproduce its own frontend and CLI. The [bootstrap reference](../selfhost/README.md) explains the seed workflow.

## Files, paths, and resources

Artifacts are built in temporary locations and published only after successful completion. A failed frontend or backend leaves an existing output intact. Source files, bundled compiler resources, and the running driver are protected from accidental overwrite through matching paths, symbolic links, and hard links.

Temporary directories are cleaned on normal exits and diagnostic errors. SIGKILL and hostile concurrent filesystem changes are outside this guarantee. Process argument storage grows dynamically; OS `exec` limits still apply.

The driver searches its build/install layout for support files. `KREMEN_SELFHOST_ROOT` can explicitly select an absolute resource directory. A bootstrap rebuild passes that resource choice to the next stage. Move an installed prefix as a whole; see [Installation](install.md).

`extern func` declares an external C symbol in the subset. Kremen checks calls against the declaration, but it cannot establish the correctness of arbitrary C implementations or foreign library ABIs. Backend diagnostics still refer to C. The subset's checked arithmetic reports a source basename, line, and column; frontend errors also show the current input excerpt.

## Work needed for full self-hosting

The subset does not yet have the full language's structs, enums, tuples, modules, slices, references, results, or cleanup semantics. Replacing the native frontend now would remove those features.

A practical next step is a typed intermediate representation, followed by transferring one subsystem at a time. Start with composite values and module resolution, then results and cleanup, then the borrow and lifetime checks. Run shared valid and invalid programs through both compilers and compare diagnostics and behavior. A full replacement needs that compatibility, not just a successful self-rebuild.

## Work needed to target hardware directly

The existing driver is a compiler command-line driver, not a kernel device driver. It runs in a user process. There is no freestanding target in this release.

A hardware target needs a runtime without POSIX/libc, a defined target ABI and data layout, startup code, a linker script, and explicit volatile/MMIO access. After those foundations, add emulator tests that boot a minimal program and exercise one small device driver. Raw pointers alone do not provide the ordering and visibility rules required for device registers.

These are planned engineering steps, not implemented features. [Safety and memory](safety.md) describes today's guarantees, and [Validation](validation.md) shows the tested host configuration.
