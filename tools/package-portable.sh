#!/bin/sh
# Assemble a Linux x86-64 distribution with an unmodified official Zig SDK.
# Existing SDKs make this operation offline. Downloads are an explicit option.
set -eu
umask 022
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BUILD_DIR="$ROOT/build/cmake"
OUTPUT_DIR="$ROOT/build/portable"
BACKEND_ROOT=
SELFHOST=
DOWNLOAD=false
ZIG_VERSION=0.15.2
usage() {
    printf '%s\n' 'Usage: sh tools/package-portable.sh --backend-root DIR [--build-dir DIR] [--output DIR] [--selfhost FILE]'
    printf '%s\n' '       sh tools/package-portable.sh --download-backend [other options]'
}
while [ "$#" -gt 0 ]; do
    case "$1" in
        --backend-root|--build-dir|--output|--selfhost)
            [ "$#" -ge 2 ] || { usage >&2; exit 2; }
            case "$1" in
                --backend-root) BACKEND_ROOT=$2 ;;
                --build-dir) BUILD_DIR=$2 ;;
                --output) OUTPUT_DIR=$2 ;;
                --selfhost) SELFHOST=$2 ;;
            esac
            shift 2 ;;
        --download-backend) DOWNLOAD=true; shift ;;
        --help|-h) usage; exit 0 ;;
        *) usage >&2; exit 2 ;;
    esac
done
[ "$(uname -s)" = Linux ] && [ "$(uname -m)" = x86_64 ] || { printf '%s\n' 'This packager targets Linux x86-64.' >&2; exit 2; }
if [ "$DOWNLOAD" = true ] && [ -n "$BACKEND_ROOT" ]; then
    printf '%s\n' 'Select --backend-root or --download-backend, not both.' >&2; exit 2
fi
mkdir -p -- "$OUTPUT_DIR"
OUTPUT_DIR=$(CDPATH= cd -- "$OUTPUT_DIR" && pwd)
if [ "$DOWNLOAD" = true ]; then
    for required in curl node sha256sum tar; do command -v "$required" > /dev/null || { printf 'Missing download tool: %s\n' "$required" >&2; exit 1; }; done
    CACHE="$OUTPUT_DIR/backend-download"
    mkdir -p -- "$CACHE"
    curl --fail --location --retry 3 --connect-timeout 20 --max-time 120 'https://ziglang.org/download/index.json' -o "$CACHE/index.json"
    node - "$CACHE/index.json" "$ZIG_VERSION" > "$CACHE/archive-info" <<'JS'
const fs = require('fs');
const catalog = JSON.parse(fs.readFileSync(process.argv[2], 'utf8'));
const version = process.argv[3];
const entry = catalog[version]?.['x86_64-linux'];
const expected = `https://ziglang.org/download/${version}/zig-x86_64-linux-${version}.tar.xz`;
if (!entry || entry.tarball !== expected || !/^[a-f0-9]{64}$/.test(entry.shasum)) {
    throw new Error('Official Zig catalog does not describe the pinned Linux SDK');
}
process.stdout.write(entry.tarball + '\n' + entry.shasum + '\n');
JS
    URL=$(sed -n '1p' "$CACHE/archive-info")
    EXPECTED=$(sed -n '2p' "$CACHE/archive-info")
    ARCHIVE="$CACHE/zig-x86_64-linux-$ZIG_VERSION.tar.xz"
    curl --fail --location --retry 3 --connect-timeout 20 --max-time 600 "$URL" -o "$ARCHIVE.partial"
    ACTUAL=$(sha256sum "$ARCHIVE.partial")
    ACTUAL=${ACTUAL%% *}
    [ "$ACTUAL" = "$EXPECTED" ] || { rm -f -- "$ARCHIVE.partial"; printf '%s\n' 'Zig archive checksum mismatch.' >&2; exit 1; }
    mv -- "$ARCHIVE.partial" "$ARCHIVE"
    tar -xJf "$ARCHIVE" -C "$CACHE"
    BACKEND_ROOT="$CACHE/zig-x86_64-linux-$ZIG_VERSION"
fi
[ -n "$BACKEND_ROOT" ] || { usage >&2; exit 2; }
BACKEND_ROOT=$(CDPATH= cd -- "$BACKEND_ROOT" && pwd)
[ -x "$BACKEND_ROOT/zig" ] && [ -d "$BACKEND_ROOT/lib" ] && [ -f "$BACKEND_ROOT/LICENSE" ] || { printf '%s\n' 'Backend root must contain the complete Zig SDK (zig, lib, LICENSE).' >&2; exit 1; }
[ "$("$BACKEND_ROOT/zig" version)" = "$ZIG_VERSION" ] || { printf 'Expected Zig %s.\n' "$ZIG_VERSION" >&2; exit 1; }
BUILD_DIR=$(CDPATH= cd -- "$BUILD_DIR" && pwd)
for binary in kremen kremen-backend; do
    [ -x "$BUILD_DIR/$binary" ] || { printf 'Missing built executable: %s/%s\n' "$BUILD_DIR" "$binary" >&2; exit 1; }
done
if [ -z "$SELFHOST" ]; then
    if [ -x "$BUILD_DIR/kremen-selfhost" ]; then SELFHOST="$BUILD_DIR/kremen-selfhost";
    elif [ -x "$ROOT/selfhost/build/kremen-selfhost" ]; then SELFHOST="$ROOT/selfhost/build/kremen-selfhost"; fi
fi
if [ -n "$SELFHOST" ] && [ ! -x "$SELFHOST" ]; then printf 'Missing self-hosted executable: %s\n' "$SELFHOST" >&2; exit 1; fi
VERSION=$(cat "$ROOT/VERSION")
NAME="kremen-v$VERSION-linux-x86_64"
WORK=$(mktemp -d "$OUTPUT_DIR/.portable-stage.XXXXXX")
trap 'rm -rf "$WORK"' EXIT HUP INT TERM
DEST="$WORK/$NAME"
mkdir -p "$DEST/bin" "$DEST/backend/bin" "$DEST/share/kremen/runtime" "$DEST/share/doc/kremen"
cp "$BUILD_DIR/kremen" "$DEST/bin/kremen"
cp -a "$BACKEND_ROOT/." "$DEST/backend/"
cp "$BUILD_DIR/kremen-backend" "$DEST/backend/bin/clang"
cp "$ROOT/runtime/kremen_rt.h" "$DEST/share/kremen/runtime/kremen_rt.h"
cp "$ROOT/LICENSE" "$DEST/LICENSE"
cp "$ROOT/LICENSE" "$DEST/share/doc/kremen/LICENSE"
cp "$ROOT/VERSION" "$DEST/VERSION"
cp "$ROOT/tools/install-portable.sh" "$DEST/install.sh"
for document in "$ROOT"/*.md; do cp "$document" "$DEST/share/doc/kremen/"; done
# Copy project learning resources without local build products or ignored A.a.
for directory in docs examples; do
    mkdir -p "$DEST/share/doc/kremen/$directory"
    (cd "$ROOT/$directory" && tar --exclude='A.a' -cf - .) | (cd "$DEST/share/doc/kremen/$directory" && tar -xf -)
done
SUBSET=no
if [ -n "$SELFHOST" ]; then
    cp "$SELFHOST" "$DEST/bin/kremen-selfhost"
    mkdir -p "$DEST/share/kremen/selfhost" "$DEST/share/kremen/native"
    for support in bootstrap.kr toolchain.kr stage0.c runtime.c runtime.h driver.c host.c host.h platform.h; do
        cp "$ROOT/selfhost/$support" "$DEST/share/kremen/selfhost/$support"
    done
    cp "$ROOT/native/platform.h" "$DEST/share/kremen/native/platform.h"
    SUBSET=yes
fi
cat > "$DEST/README-PORTABLE.md" <<DOC
# Kremen $VERSION portable package

Run \`./bin/kremen doctor\`, then \`./bin/kremen run share/doc/kremen/examples/hello.kr\`.
The package uses its bundled backend automatically when CC and --cc are omitted.
No download, system C compiler, or system PATH modification is required to use it.
The Zig backend keeps its own compilation cache in the user's cache directory.
To copy the prebuilt files into a prefix, run \`sh install.sh --prefix /absolute/path\`.
The same script accepts \`--uninstall\`; it removes only its owned manifest files.

This archive targets Linux x86-64. Release builds link the frontend statically;
the backend adapter compiles generated programs with its bundled static musl runtime.
Operating-system services and a compatible Linux kernel remain necessary.
Self-hosted subset included: $SUBSET.

Kremen's license is in LICENSE. The complete unmodified Zig $ZIG_VERSION SDK,
including its license and bundled LLVM/libc notices, is in backend. The tiny
backend/bin/clang adapter forwards arguments to backend/zig cc without a shell.
See share/doc/kremen/docs for language, platform, and installation instructions.
DOC
printf 'Kremen version: %s\nBackend: Zig %s\nTarget: linux-x86_64\nSelf-hosted subset: %s\n' "$VERSION" "$ZIG_VERSION" "$SUBSET" > "$DEST/PACKAGE-INFO.txt"
(cd "$DEST" && find . -type f ! -name SHA256SUMS -print0 | LC_ALL=C sort -z | xargs -0 sha256sum) > "$DEST/SHA256SUMS"
ARCHIVE="$OUTPUT_DIR/$NAME-portable.tar.gz"
tar -C "$WORK" -czf "$ARCHIVE.partial" "$NAME"
mv -- "$ARCHIVE.partial" "$ARCHIVE"
(cd "$OUTPUT_DIR" && sha256sum "$(basename "$ARCHIVE")") > "$ARCHIVE.sha256"
printf 'Created %s\n' "$ARCHIVE"
