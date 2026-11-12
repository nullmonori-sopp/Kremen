# Kremen on Windows

Kremen 0.4.8 runs as native Windows programs. The full compiler and bootstrap-subset driver use Windows process and filesystem operations directly. Full-language workers use Windows thread primitives through the C runtime; a pthread compatibility DLL, MSYS2, and WSL are no longer needed.

Use the Windows x86-64 portable package when you want to write Kremen programs. Build from source when you want to change the compiler. These workflows have different prerequisites.

## Use the portable package

Unpack the entire Windows package into a directory you choose. Open PowerShell in that directory:

```powershell
.\bin\kremen.exe --version
.\bin\kremen.exe doctor
.\bin\kremen.exe run .\share\doc\kremen\examples\hello.kr
```

The final command prints `Hello, Kremen!`. The matching backend and runtime are already inside the package. Keep `bin`, `share`, and `backend` together when moving it. [Portable packages](portable.md) explains the layout, backend selection, licenses, offline use, and cache requirements.

You can create your own source file outside the package and pass its path to `check`, `build`, or `run`. Quote paths containing spaces:

```powershell
.\bin\kremen.exe check 'C:\My projects\main.kr' --mode=learn
.\bin\kremen.exe build 'C:\My projects\main.kr' -o 'C:\My projects\app.exe'
& 'C:\My projects\app.exe'
```

The `&` is PowerShell's invocation operator for the quoted executable name. It is not part of Kremen syntax. Work remains the default profile; learn adds the requirements in [Work and learn profiles](profiles.md).

Without `-o`, `build` chooses `a.exe`. An explicit output name is used as written, so include `.exe` for an ordinary Windows executable name. `run` builds in a private temporary directory and removes its executable afterward. Compiler options belong before the end-of-options marker `--`.

## Diagnose the selected backend

`doctor` compiles and runs a small program with the actual Kremen runtime and strict C11 warnings. Its probe starts and joins a worker and checks the returned value:

```powershell
.\bin\kremen.exe doctor
.\bin\kremen.exe explain E016
```

A self-check failure uses `E016`; an unreadable runtime is a separate `E010`. Read the selected compiler and runtime in a successful report. An explicit `--cc` or inherited `CC` can override the bundled backend. Remove an unintended override before diagnosing a portable package.

The probe runs the result, so `doctor` checks the current host, not a cross-compilation target. It does not replace language regressions. External compiler messages retain their own format, including when JSON is selected for Kremen errors.

## Build Kremen from source

A source archive has no prebuilt compiler or SDK. A native source build needs LLVM/Clang, the Windows SDK, and the C build tools that provide system headers and libraries. Open a Developer PowerShell configured for those tools. Use PowerShell 7 for the build and bootstrap scripts:

```powershell
.\build.ps1 -Compiler clang -Selfhost
.\build\kremen.exe --version
.\build\kremen.exe doctor --cc clang
.\build\kremen.exe run .\examples\hello.kr --cc clang
```

`-BuildDir` selects another full-compiler output directory. `-Selfhost` also runs the subset's native bootstrap workflow. Without it, the script builds the full compiler and backend launcher. This development setup is unnecessary for a complete portable package.

The source build uses the portable C implementation of byte copying. The Linux assembly implementation is not used on Windows. The build scripts preserve strict warnings as errors; platform support is not enabled by disabling checks.

The bootstrap subset has its own `selfhost/bootstrap.ps1` workflow. It verifies the committed seed against successive generated C stages before publishing the driver. See [Self-hosting](selfhosting.md) for its commands and language limits. Compiling the subset on Windows does not mean the full language's frontend has become self-hosted.

To install a complete portable package into your user profile, run its `install.ps1`. The same installer can copy an already built source tree. [Installation](install.md#windows-source-builds-and-installation) explains `-Prefix`, `-SourceRoot`, upgrades, and removal; the package can also be used directly without installation.

## Paths and Unicode

Compiler arguments and language filesystem paths are UTF-8 internally. The Windows boundary converts them to UTF-16 for the native APIs. It reads the original wide command line, handles quoted arguments without invoking a command shell, and uses wide operations for source files, temporary directories, executable discovery, and filesystem built-ins.

Normal Unicode paths, spaces, and shell metacharacters can therefore be used without switching the system ANSI code page. Filesystem directory names returned to Kremen are encoded as UTF-8. Embedded NUL and invalid path encodings are rejected rather than passed through a lossy conversion.

Existing source paths are resolved through Windows file handles. Symlinks and junctions are followed before checking the selected project root. File identities protect source and toolchain resources from output aliases, including hard links and case aliases. A root ancestor's identity is checked as well, so case-sensitive directories do not become interchangeable merely because their spellings differ only by case.

Imports remain local and relative. Drive-qualified paths and UNC paths do not become valid import expressions; use an explicit wider `--root` for a deliberately shared source directory. Ordinary source/output command-line paths are separate from import syntax.

UTF-8 path support does not promise that every Windows namespace or extended path length is accepted. Host path limits, access permissions, device names, and filesystem behavior still apply. Do not use unpaired UTF-16 surrogates as portable filenames.

## Files and threads

Filesystem streams use binary modes. `r`, `w`, and `a` are aliases of `rb`, `wb`, and `ab`; CR, LF, NUL, and `0x1a` remain file data. File sizes use 64-bit metadata. `fsRename` can replace an existing file without a delete-first gap, and a failed replacement leaves the old target in place. Directory replacement and locked-file behavior follow Windows rules.

`fmt -w` refuses to rewrite a read-only source and keeps its temporary file removable on failure. Its mode preservation covers the C runtime's read/write flags, not arbitrary NTFS access-control lists.

Workers use the existing integer-job API. The implementation starts CRT-aware Windows threads and uses native synchronization for the shared runtime streams and diagnostics. The same creator ownership, worker-local resources, deferred cleanup, and join rules apply on Linux. General shared-memory concurrency, timed joins, and forced worker cancellation are still absent.

A generated program includes its Kremen runtime. It does not load `libwinpthread-1.dll` or an MSYS compatibility runtime. Other code linked through an explicitly selected external backend can have its own deployment requirements; the portable package's tested backend is the default path.

## Validate the Windows build

The Windows CI uses native PowerShell and Clang, then exercises generated programs, the bootstrap workflow, Unicode paths, and a portable package with development compilers removed from `PATH`. The portable check also clears `CC`, so an inherited override cannot silently select the CI machine's compiler.

Read [Validation](validation.md) for the actual suite results and supported host baseline. Linux signal, FIFO, file-size-limit, and sanitizer cases remain distinct from a Windows result. A successful native test does not establish every Windows release, ARM Windows, a kernel target, or arbitrary foreign C-library compatibility.
