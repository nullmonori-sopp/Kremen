# Kremen on Windows

Kremen 0.4.7 begins native Windows support with one defined build environment: Windows x86-64, MSYS2 UCRT64, MinGW-w64 GCC, and winpthreads. The result is a Windows `.exe`; WSL is not involved. MSYS2 supplies the shell, build tools, C compiler, and pthread implementation used by this first profile.

Start with this environment even if another C compiler is already installed. The initial profile does not include MSVC, ARM Windows, the self-hosted subset, or a standalone graphical installer. [Validation](validation.md) distinguishes Linux regressions from the Windows checks.

## Set up the tools

Install MSYS2 from its official distribution and complete its normal package updates. Open the terminal named **MSYS2 UCRT64**, then install the development tools:

```sh
pacman -S --needed base-devel mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-winpthreads
```

Package installation needs a network connection. Once these tools and the Kremen source archive are present, building and using Kremen does not need one.

Check the selected environment:

```sh
printf '%s\n' "$MSYSTEM"
command -v gcc
gcc -dumpmachine
```

The first command should print `UCRT64`. GCC should come from `/ucrt64/bin`, and its target should be `x86_64-w64-mingw32`. Keep that UCRT64 terminal open for the commands below. A plain MSYS terminal selects a different environment.

## Build and run your first program

Unpack the source archive into a path using ASCII characters. Spaces are supported. Open that directory in the UCRT64 terminal and run:

```sh
make CC=gcc USE_ASM=0
./build/kremen.exe --version
./build/kremen.exe doctor --cc gcc
./build/kremen.exe run examples/tutorial/01_hello.kr --cc gcc
```

The last command should print `Hello, Kremen!`. `doctor` first checks that the selected compiler can compile and execute a small program with strict C flags and the matching Kremen runtime. Its probe also starts and joins one integer worker. If it fails, read the reported operation and run `kremen explain E016`; a missing runtime is a separate `E010` error.

`doctor` executes the result, so it is a native setup check rather than a cross-compilation test. It does not run the entire language test suite. External C-tool messages retain their own format, including when Kremen's JSON error format is selected.

## Everyday commands

```sh
./build/kremen.exe check examples/learn/checked_input.kr --mode=learn
./build/kremen.exe build examples/hello.kr --cc gcc -o build/hello.exe
./build/hello.exe
./build/kremen.exe emit-c examples/hello.kr -o build/hello.c
gcc -std=c11 -Wall -Wextra -Wpedantic -Werror -pthread build/hello.c -o build/hello.exe
./build/kremen.exe fmt --check examples/learn/checked_input.kr
./build/kremen.exe explain E131
```

Without `-o`, `build` chooses `a.exe` on Windows. An explicit output name is used as written, so include `.exe` when you want an ordinary Windows executable name. `run` chooses its own temporary executable and removes it afterward. Compiler options, including `--cc`, belong before the end-of-options marker `--`.

Both work and learn use the same Windows backend and runtime checks. The additional learning rules are described in [Work and learn profiles](profiles.md). The separate `kremen selfhost` command reports that its POSIX subset is unsupported on Windows in this release.

## Install into a prefix

From the UCRT64 terminal:

```sh
CC=gcc sh install.sh --prefix "$HOME/.local"
export PATH="$HOME/.local/bin:$PATH"
kremen.exe doctor --cc gcc
```

On Windows the source installer installs the full-language `kremen.exe`, its runtime, documentation, and examples. It omits the POSIX subset compiler and rebuild resources. The installer uses the same ownership manifest and `--uninstall` interface as the Linux installation; see [Installation](install.md).

Keep the UCRT64 compiler and DLL directories available. Generated threaded programs depend on the matching winpthreads runtime, commonly `libwinpthread-1.dll`, and may need other DLLs supplied by the selected C toolchain. Moving an executable alone does not bundle those dependencies. This release supplies source and a source installer rather than an independent Windows binary distribution.

The `.exe` is a native Windows program. Launching it from PowerShell or another terminal still requires the selected backend and its dependencies on that terminal's `PATH`. The UCRT64 workflow above is the supported starting point.

## Paths, files, and limits

The frontend resolves existing paths through Windows file handles. It follows symlinks and junctions for the project-root check, compares canonical module paths without case sensitivity, and uses file identity to protect sources and toolchain files from output aliases. Import paths remain local and relative: drive-qualified and UNC imports are rejected. Select a deliberate wider `--root` when a shared source directory belongs to the project.

The first implementation uses narrow Windows and C-runtime path APIs. Use ASCII paths for a predictable result. Complete arbitrary-Unicode paths, extended long paths, and every Windows filesystem configuration are not established by this profile.

The native executable accepts Windows paths, including `C:/project/main.kr`. MSYS2 normally converts its own paths before launching native tools, but its heuristics can leave unusual arguments unchanged. When passing an absolute MSYS2 path containing apostrophes or shell characters, convert it explicitly with `cygpath -m` and keep the result quoted. A native process cannot resolve MSYS2's private `/tmp` mount by itself.

Filesystem streams use binary modes. The bytes `\r`, `\n`, NUL, and `0x1a` remain file data rather than being changed by Windows text translation. This concerns Kremen's filesystem operations; standard input, standard output, standard error, and the terminal's encoding retain their host conventions.

On Windows, `fmt -w` refuses to rewrite a read-only source. Its staged file remains removable on that failure. Formatter mode preservation covers the C runtime's read/write flags; it does not copy arbitrary NTFS access-control lists.

File sizes use 64-bit metadata. `fsRename` supports replacing a destination file and preserves it when the move fails; directory replacement can still fail under Windows rules. A rename between names for the same file identity succeeds without deleting the file. Filesystem errors remain result codes that can be interpreted with `errorMessage`.

The full runtime uses winpthreads for the existing integer-worker API. It does not add shared mutable references, closures, or new thread-transfer rules. Freestanding targets and an independent machine-code backend remain separate work.

## Run the Windows checks

```sh
make CC=gcc USE_ASM=0 test-windows
```

The Windows checks exercise the selected native profile. The broader Linux suite includes POSIX bootstrap, signal, file-limit, and full-device scenarios that are not interchangeable with a Windows test result. Keep the distinction when reporting a build. The [validation record](validation.md) describes the actual coverage and remaining platform limits.
