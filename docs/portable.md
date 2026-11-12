# Use a portable Kremen package

A portable package is a ready-to-run directory for one host platform. It includes Kremen and the C backend that turns generated C into executable programs. You do not need to install GCC, Clang, MSYS2, WSL, or Visual Studio to use that package. The package still uses your operating system's normal executable loader and system services.

Choose the Linux x86-64 or Windows x86-64 package for the computer that will run Kremen. A source archive is different: it contains the compiler sources and build scripts and needs development tools before it can produce executables. [Installation](install.md) explains source builds.

| Host | Portable archive |
| --- | --- |
| Linux x86-64 | `kremen-v0.4.8-linux-x86_64-portable.tar.gz` |
| Windows x86-64 | `kremen-v0.4.8-windows-x86_64-portable.zip` |

Each portable archive has its own external `.sha256` file, and an internal `SHA256SUMS` lists the packaged files. Keep the download's directory structure when extracting it.

## Keep the directory together

After unpacking, the important directories are:

| Directory | What it contains |
| --- | --- |
| `bin/` | Full-language compiler and bootstrap-subset driver |
| `share/kremen/` | Matching runtime, bootstrap resources, and native platform support |
| `share/doc/kremen/` | Documentation, examples, and project license |
| `backend/` | Bundled Zig SDK, its licenses, and the small `bin/clang` launcher |

The launcher's name is `clang.exe` on Windows. It forwards separate arguments to the SDK's `zig cc` command; it does not interpret them as a shell command. Kremen finds this launcher relative to its executable.

Moving the whole unpacked directory keeps those relative paths valid. Moving only `kremen.exe` or `kremen` does not create a complete installation. Keep the SDK's `lib/` and license files too: the backend needs more than its executable.

## First run on Linux

Open a terminal in the unpacked directory:

```sh
./bin/kremen --version
./bin/kremen doctor
./bin/kremen run share/doc/kremen/examples/hello.kr
```

The example prints `Hello, Kremen!`. `doctor` compiles and runs a small probe with the selected backend and actual runtime, including a worker spawn and join. This catches an incomplete extraction, missing backend resources, or an incompatible host.

For a personal program outside the package:

```sh
./bin/kremen build /absolute/path/to/main.kr -o /absolute/path/to/app
/absolute/path/to/app
```

The Linux release binaries are linked statically. The bundled backend selects `x86_64-linux-musl` and links generated programs statically too, so those programs do not depend on the host's glibc ABI. They still need a compatible Linux kernel and x86-64 CPU. This does not make one executable work on another OS, every historical kernel, or a different architecture. See [Validation](validation.md) for the tested hosts.

To copy the portable directory into a user prefix:

```sh
sh install.sh --prefix "$HOME/.local/kremen"
"$HOME/.local/kremen/bin/kremen" doctor
```

This `install.sh` belongs to the portable package and copies its built files and SDK. It is different from the source archive's build-and-install script. Its ownership manifest supports `--uninstall`, refuses unrelated occupied paths, and keeps unrelated files in the selected prefix.

## First run on Windows

Open PowerShell in the unpacked directory:

```powershell
.\bin\kremen.exe --version
.\bin\kremen.exe doctor
.\bin\kremen.exe run .\share\doc\kremen\examples\hello.kr
```

No Unix shell is involved. The Windows frontend decodes its native command line as UTF-16 and exposes UTF-8 paths to the language and compiler internals. Source paths, temporary paths, and filesystem built-ins use wide Windows operations. The [Windows guide](windows.md) describes path handling, source builds, and host-specific filesystem behavior.

The package's `install.ps1` can copy it into a selected user prefix and later remove its owned files. Installation is optional; direct paths into an unpacked package work as well. See [Installation](install.md#windows-source-builds-and-installation) for the parameters.

## How backend selection works

An explicit `--cc` chooses one compiler executable. The `CC` environment variable can also override the default. Otherwise, the portable package selects its bundled launcher. Ordinary source builds can fall back to the host compiler when no bundled backend exists.

If `doctor` reports an unexpected compiler, check whether your terminal inherited `CC` from another development environment. Removing that override lets the portable package use its own backend. The successful doctor report names the selected compiler and runtime.

The package uses Zig 0.15.2 as a C/LLVM backend. Kremen remains a C-written full frontend with a Kremen-written bootstrap subset; bundling a backend does not turn the full compiler into a self-hosted implementation or a direct machine-code backend.

## Offline use and writable data

Ordinary checking, formatting, compilation, explanations, and execution work without a network connection after the package is complete. Downloading the SDK belongs to package creation, not to a user's first program. The packaging scripts verify the downloaded archive against the selected version's published checksum before extraction.

Compilation still needs writable temporary and cache storage. Put your programs and output files somewhere writable. A package directory can be read-only when its backend's temporary/cache locations remain writable; read-only code and resources do not mean the whole operating system can be read-only.

The SDK is distributed with its license material. Keep those files when copying or redistributing the package. Kremen's own license is in `share/doc/kremen/LICENSE`.

## What is shared across the two systems

The language rules, work/learn profiles, result handling, borrow checks, and runtime resource model are shared. A private host boundary implements processes, threads, paths, and directory operations using the available OS primitives. Windows does not need a pthread-emulation DLL. Linux uses the POSIX-thread interface: ordinary source builds link their selected system implementation, while the portable backend supplies its statically linked musl implementation.

An operating system's API is still necessary to open files and create processes. Eliminating compatibility libraries does not eliminate that boundary. Filesystem permissions, locked files, signal behavior, executable suffixes, and path conventions can differ between hosts. The runtime reports those failures through the existing result and diagnostic interfaces.

Read [Safety and memory](safety.md) for language guarantees and [Self-hosting](selfhosting.md) for the smaller bootstrap frontend.
