# Installing Kremen

The release archive contains source code and an installer. The installer builds the compiler on your computer, then copies both toolchains, their support files, documentation, and examples into a directory you choose.

## Requirements

The tested release platform is Linux x86-64 with GCC, libc, and POSIX threads. Install a C11 compiler, POSIX thread headers and linking support, `make`, a POSIX shell, and ordinary tools including `install`, `mktemp`, `find`, `sort`, `cp`, and `mv`. These normally come with the system C development tools. Use your operating system's package manager if one is missing. Python and network access are not required to build Kremen.

A C compiler remains necessary for `kremen build`, `kremen run`, and bootstrap rebuilds. The full compiler's `check`, `fmt`, and `emit-c` operations do not invoke it. The self-hosted subset's `check` also checks the generated C, so that command does need it.

Full-language `build` and `run` add `-pthread` automatically. Include that flag yourself when compiling output from `emit-c`. The separate bootstrap subset does not implement worker threads and keeps its existing C-backend flags.

The Makefile selects assembly only on Linux x86-64. `make USE_ASM=0` forces the portable C implementation. Other POSIX platforms have code paths but have not been validated as release targets. Packaging additionally needs GNU tar/find/sort, `zip`, `sha256sum`, and `xargs`; the source-input tests use `timeout`.

## Unpack and verify

Place the release files in one directory:

```text
kremen-v0.4.6.zip
kremen-v0.4.6.tar.gz
kremen-v0.4.6.sha256
```

If you downloaded both archives, verify them and unpack either one:

```sh
sha256sum -c kremen-v0.4.6.sha256
unzip kremen-v0.4.6.zip
cd kremen
sha256sum -c SHA256SUMS
```

For the tar archive, use `tar -xzf kremen-v0.4.6.tar.gz` instead of `unzip`. The external checksum file lists both archives; a missing archive is reported as missing. If you downloaded only one, compare its `sha256sum` output with its line in the checksum file. Checksums detect changed bytes; they are not a publisher signature.

`kremen.zip` is a copy of the current ZIP release. Its contents match `kremen-v0.4.6.zip`.

## Choose local use or installation

For local use from the source directory:

```sh
make
./build/kremen run examples/tutorial/01_hello.kr
```

No installation is required. [Your first program](getting-started.md) uses this workflow.

For an installation in your home directory:

```sh
sh install.sh
export PATH="$HOME/.local/bin:$PATH"
kremen --version
kremen selfhost --version
```

The default prefix is `$HOME/.local`. The prefix is the directory under which `bin/` and `share/` are created. Adding its `bin/` to `PATH` lets the shell find `kremen` by name. The `export` command affects the current shell; the installer does not modify startup files.

To use a different prefix or C compiler:

```sh
CC=/absolute/path/to/gcc sh install.sh --prefix /absolute/path/to/kremen
/absolute/path/to/kremen/bin/kremen --version
```

`CC` names one executable, not a shell command containing extra flags. The prefix must be absolute, writable, and different from `/`. Spaces are supported. If `HOME` is unset, supply `--prefix`. The installer does not run `sudo` or download dependencies.

## What gets installed

| Path inside the prefix | Contents |
| --- | --- |
| `bin/kremen` | Full-language native compiler and formatter |
| `bin/kremen-selfhost` | Kremen-written compiler and CLI for the bootstrap subset |
| `share/kremen/runtime/kremen_rt.h` | Runtime embedded in generated full-language C |
| `share/kremen/selfhost/` | Bootstrap sources, seed, runtime, host primitives, and platform header |
| `share/kremen/install.marker`, `install.manifest` | Ownership marker and installed-file list |
| `share/doc/kremen/` | License, English Markdown documentation, examples, and saved benchmark reports |

Installation compiles both toolchains before changing the destination. Repeating the command upgrades an installation owned by this installer. It refuses to overwrite an unrelated compiler or occupied data directory without a valid ownership marker.

You can move the whole prefix. The tools find resources relative to their executable. Moving just one compiler binary without its data is not a complete installation. Programs you build embed the full runtime and can run independently of the source tree, on a compatible host platform.

A separately rebuilt bootstrap binary can use an explicit absolute `KREMEN_SELFHOST_ROOT`. That directory must contain `bootstrap.kr`, `toolchain.kr`, `stage0.c`, `runtime.c`, `runtime.h`, `host.c`, `host.h`, `platform.h`, and `driver.c`. See [Self-hosting](selfhosting.md).

## Remove an installation

Keep a copy of `install.sh` from the source distribution:

```sh
sh install.sh --prefix /absolute/path/to/kremen --uninstall
```

Use the same prefix you installed into, or its current path if you moved it. Removal validates the manifest, removes owned files, and removes only empty Kremen directories. Other files in the prefix are preserved.

## Check or rebuild a release

From the source directory:

```sh
make test
make test-sanitize
make test-install
make dist
```

`make dist` creates versioned ZIP/tar.gz archives and external checksums next to the source directory, and refreshes `SHA256SUMS` inside it. Use `sh tools/package.sh /absolute/output` for another output directory. Build directories, Python files, caches, and existing archives are excluded. Packaging does not run the tests for you.

The [validation record](validation.md) lists what was exercised. [Repository update instructions](repository-update.md) explain which source and generated seed files to commit.
