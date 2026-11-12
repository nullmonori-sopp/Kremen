# Installing Kremen

Choose a portable binary package if you want to write programs now. Choose a source archive if you want to build or modify the compiler. A portable package includes a matching C backend; a source archive needs development tools before it can build executables.

| Download | Ready-to-run compiler | Included backend | First step |
| --- | --- | --- | --- |
| Linux x86-64 portable package | Yes | Zig C/LLVM SDK | Unpack and run `bin/kremen doctor` |
| Windows x86-64 portable package | Yes | Zig C/LLVM SDK | Unpack and run `bin/kremen.exe doctor` in PowerShell |
| Source ZIP or tar.gz | No | No | Build with the source instructions below |

[Portable packages](portable.md) explains the ready-to-run layout, offline operation, licenses, and backend selection. [Windows setup](windows.md) covers native PowerShell use, source builds, and path behavior. The Windows portable package does not require MSYS2, WSL, winpthreads, Visual Studio, or a compiler on the system `PATH`.

## Verify and unpack source archives

The source release files are:

```text
kremen-v0.4.8.zip
kremen-v0.4.8.tar.gz
kremen-v0.4.8.sha256
```

If you downloaded both archives, verify them and unpack either one:

```sh
sha256sum -c kremen-v0.4.8.sha256
unzip kremen-v0.4.8.zip
cd kremen
sha256sum -c SHA256SUMS
```

For the tar archive, use `tar -xzf kremen-v0.4.8.tar.gz`. The external checksum file lists both source archives; a missing one is reported as missing. If you downloaded only one, compare its checksum with its line in the external file. A checksum detects changed bytes; it is not a publisher signature.

`kremen.zip` is an alias of the current source ZIP. Source archives contain build scripts, documentation, examples, tests, and the verified C seed. They exclude generated executables, build directories, SDK downloads, caches, and Python files. Portable packages are separate platform-specific artifacts.

## Build from source on Linux

Install a C11 compiler, system POSIX-thread development support, `make`, a POSIX shell, and ordinary tools such as `install`, `mktemp`, `find`, `sort`, `cp`, and `mv`. These normally come with the system development tools. Python and a network connection are not required once the source and build tools are present.

From the source directory:

```sh
make all selfhost
./build/kremen doctor
./build/kremen run examples/tutorial/01_hello.kr
```

The full compiler is `build/kremen`; the bootstrap-subset driver is `selfhost/build/kremen-selfhost`. The full-language frontend's `check`, `fmt`, `explain`, and `emit-c` do not invoke a C backend. `build`, `run`, and bootstrap rebuilds do. The subset's `check` also syntax-checks generated C, so it needs a backend.

Generated full-language C uses strict warnings as errors. POSIX builds add `-pthread` for system threading. The Windows adapter uses native thread primitives and does not link a pthread-emulation library. If invoking an external C compiler yourself, use the flags for that host; the built-in commands select them automatically.

The Makefile chooses assembly only on Linux x86-64. `make USE_ASM=0` forces the portable C copy routine. Other CPU architectures and hosts need their own validation; a common C implementation alone does not establish a tested release target.

## Install Linux source builds into a prefix

For installation in your home directory:

```sh
sh install.sh
export PATH="$HOME/.local/bin:$PATH"
kremen --version
kremen selfhost --version
```

The default prefix is `$HOME/.local`. It is the directory under which `bin/` and `share/` are created. Adding `bin/` to `PATH` lets your shell find Kremen by name. The installer does not edit startup files, run `sudo`, or download a compiler backend.

To select another prefix or compiler:

```sh
CC=/absolute/path/to/gcc sh install.sh --prefix /absolute/path/to/kremen
/absolute/path/to/kremen/bin/kremen doctor
```

`CC` names one executable, not a shell command containing extra flags. The prefix must be absolute, writable, and different from `/`. Spaces are supported. Supply `--prefix` when `HOME` is unset.

| Path inside the prefix | Contents |
| --- | --- |
| `bin/kremen` | Full-language compiler and formatter |
| `bin/kremen-selfhost` | Kremen-written compiler and CLI for the bootstrap subset |
| `share/kremen/runtime/kremen_rt.h` | Runtime embedded in generated full-language C |
| `share/kremen/selfhost/` | Bootstrap sources, seed, runtime, and host primitives |
| `share/kremen/native/` | Native platform support needed by installed bootstrap resources |
| `share/kremen/install.marker`, `install.manifest` | Ownership marker and installed-file list |
| `share/doc/kremen/` | Project license, English guides, examples, and saved benchmark reports |

The source installer compiles both toolchains before changing the destination. Repeating it upgrades an installation owned by its marker and manifest. It refuses an unrelated compiler or occupied data directory without valid ownership information.

A source installation normally uses your selected system backend. A portable package additionally contains `backend/` and chooses its matching bundled launcher unless `CC` or `--cc` overrides it. Keep this distinction when comparing prerequisites.

## Windows source builds and installation

Use the native `build.ps1` and `selfhost/bootstrap.ps1` workflows described in [Windows setup](windows.md) and [Self-hosting](selfhosting.md). They use native Clang and the Windows SDK rather than a Unix compatibility shell. Those development tools are needed to rebuild Kremen itself, not to use a complete portable package.

The Windows portable package contains `bin/kremen.exe`, the native subset driver, their matching resources, and the bundled backend. It can be kept in any suitable directory and moved as a whole. Add its `bin` directory to your own `PATH` if desired; explicit paths work without changing system settings.

To copy a portable package into a user-owned prefix, run its installer from PowerShell:

```powershell
.\install.ps1 -Prefix "$env:LOCALAPPDATA\Programs\Kremen"
& "$env:LOCALAPPDATA\Programs\Kremen\bin\kremen.exe" doctor
```

`install.ps1` defaults to that prefix and to the directory containing the script as its source. `-SourceRoot` selects another prepared package or source directory. It does not require administrator privileges or edit `PATH`. It validates its ownership marker and manifest before upgrading or removing an existing installation, and refuses unrelated occupied paths.

For a source installation, build first in a Developer PowerShell with native Clang and the Windows SDK:

```powershell
.\build.ps1 -Compiler clang -Selfhost
.\install.ps1 -Prefix "$env:LOCALAPPDATA\Programs\Kremen"
```

The source installer can invoke the build when no executable exists yet. This source-build path does not add a bundled backend automatically. Use the complete portable archive when the destination computer has no development tools.

## Relocation and removal

Move an installed prefix or portable directory as a whole. The compilers discover resources and the bundled backend relative to their actual executable. A lone compiler binary is not a complete installation. Generated programs embed the Kremen runtime, so they do not need the source tree alongside them; they still target a particular OS, architecture, and system ABI.

A separately rebuilt bootstrap binary can use an explicit absolute `KREMEN_SELFHOST_ROOT`. See [Self-hosting](selfhosting.md) for its resource requirements.

For an installation made by `install.sh`, keep that source installer and use the selected prefix's current location:

```sh
sh install.sh --prefix /absolute/path/to/kremen --uninstall
```

Removal validates the manifest before changing anything, removes owned files, and removes only empty Kremen directories. Other files in the prefix remain. To remove a portable extraction, keep any projects or progress you placed inside it, then remove that extraction using your operating system's file manager or shell.

For a Windows installation made by `install.ps1`, use the current prefix location:

```powershell
.\install.ps1 -Prefix "$env:LOCALAPPDATA\Programs\Kremen" -Uninstall
```

The PowerShell installer removes only the validated owned files. It can leave empty directories behind. Keep your own projects outside the installation or save them before removing an unpacked portable directory.

## Create packages from a checkout

The source archive command is:

```sh
make dist
```

It creates versioned ZIP/tar.gz archives and external checksums next to the source directory and refreshes `SHA256SUMS`. `sh tools/package.sh /absolute/output` selects another destination. Packaging does not run tests automatically.

Portable packages are created by `tools/package-portable.sh` or `tools/package-portable.ps1`. They take a built compiler/launcher and a complete matching SDK. The SDK can be provided from an already verified local directory for offline packaging; an explicit download option fetches the selected SDK release and verifies its archive before extraction. The [portable guide](portable.md) explains the resulting layout and license material.

[Validation](validation.md) records release checks. [Repository update instructions](repository-update.md) lists source changes; binary archives belong in release assets rather than the source tree.
