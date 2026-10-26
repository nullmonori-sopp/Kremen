#!/bin/sh
# Build and install locally. Does not use a network connection or change shell rc files.
set -eu

SOURCE_ROOT=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
PREFIX=
if [ -n "${HOME:-}" ]; then PREFIX="$HOME/.local"; fi
ACTION=install
usage() {
    printf '%s\n' 'Usage: sh install.sh [--prefix /absolute/path] [--uninstall]'
    printf '%s\n' 'Default prefix: $HOME/.local. Requires make, a C11 compiler and POSIX tools.'
}
while [ "$#" -gt 0 ]; do
    case "$1" in
        --prefix)
            [ "$#" -ge 2 ] || { usage >&2; exit 2; }
            PREFIX=$2; shift 2 ;;
        --uninstall) ACTION=uninstall; shift ;;
        --help|-h) usage; exit 0 ;;
        *) usage >&2; exit 2 ;;
    esac
done
while [ "${PREFIX%/}" != "$PREFIX" ]; do PREFIX=${PREFIX%/}; done
case "$PREFIX" in
    /*) ;;
    *) printf '%s\n' 'Installation prefix must be an absolute path other than /.' >&2; exit 2 ;;
esac
case "$PREFIX" in
    *'
'*|*/../*|*/..|*/./*|*/.) printf '%s\n' 'Prefix contains an unsupported path component.' >&2; exit 2 ;;
esac

MARKER="$PREFIX/share/kremen/install.marker"
MANIFEST="$PREFIX/share/kremen/install.manifest"
OWNED=false
if [ -f "$MARKER" ] && [ "$(cat "$MARKER")" = 'kremen-native-install-v1' ] && [ -f "$MANIFEST" ]; then OWNED=true; fi

# Validate the whole manifest before removing or replacing any installed files.
validate_manifest() {
    while IFS= read -r relative; do
        case "$relative" in
            *'/../'*|../*|*/..|/*|*'/./'*|*'//'*|'') return 1 ;;
        esac
        case "$relative" in
            bin/kremen|bin/kremen-selfhost|share/kremen/selfhost/*|share/kremen/runtime/kremen_rt.h|share/kremen/install.marker|share/kremen/install.manifest|share/doc/kremen/*) ;;
            *) return 1 ;;
        esac
    done < "$MANIFEST"
}
if [ "$OWNED" = true ]; then
    validate_manifest || { printf '%s\n' 'Invalid Kremen installation manifest; no files changed.' >&2; exit 1; }
fi
if [ "$ACTION" = uninstall ]; then
    [ "$OWNED" = true ] || { printf '%s\n' 'No installation owned by this installer at the selected prefix.' >&2; exit 1; }
    # Keep the manifest available until all other entries have been removed.
    while IFS= read -r relative; do
        [ "$relative" = share/kremen/install.manifest ] || rm -f -- "$PREFIX/$relative"
    done < "$MANIFEST"
    rm -f -- "$MANIFEST"
    find "$PREFIX/share/kremen" "$PREFIX/share/doc/kremen" -depth -type d -exec rmdir -- {} \; 2>/dev/null || :
    printf 'Removed Kremen from %s\n' "$PREFIX"
    exit 0
fi

for command in make cc install mktemp find sort; do
    if [ "$command" = cc ]; then command=${CC:-cc}; fi
    command -v "$command" > /dev/null 2>&1 || { printf 'Missing build tool: %s\n' "$command" >&2; exit 1; }
done
if [ "$OWNED" != true ]; then
    for reserved in bin/kremen bin/kremen-selfhost share/kremen share/doc/kremen; do
        if [ -e "$PREFIX/$reserved" ] || [ -L "$PREFIX/$reserved" ]; then
            printf 'Refusing to overwrite an unowned path: %s\n' "$PREFIX/$reserved" >&2
            exit 1
        fi
    done
fi

# Finish compilation before modifying the destination.
make -C "$SOURCE_ROOT" all selfhost
WORK=$(mktemp -d "${TMPDIR:-/tmp}/kremen-install.XXXXXX")
trap 'rm -rf "$WORK"' EXIT HUP INT TERM
mkdir -p "$WORK/bin" "$WORK/share/kremen/runtime" "$WORK/share/doc/kremen"
install -m 755 "$SOURCE_ROOT/build/kremen" "$WORK/bin/kremen"
install -m 755 "$SOURCE_ROOT/selfhost/build/kremen-selfhost" "$WORK/bin/kremen-selfhost"
mkdir -p "$WORK/share/kremen/selfhost"
for support in bootstrap.kr toolchain.kr stage0.c runtime.c runtime.h driver.c host.c host.h platform.h; do
    install -m 644 "$SOURCE_ROOT/selfhost/$support" "$WORK/share/kremen/selfhost/$support"
done
install -m 644 "$SOURCE_ROOT/runtime/kremen_rt.h" "$WORK/share/kremen/runtime/kremen_rt.h"
if [ -f "$SOURCE_ROOT/LICENSE" ]; then
    install -m 644 "$SOURCE_ROOT/LICENSE" "$WORK/share/doc/kremen/LICENSE"
fi
for document in "$SOURCE_ROOT"/*.md; do
    install -m 644 "$document" "$WORK/share/doc/kremen/$(basename -- "$document")"
done
for directory in docs bench selfhost; do
    mkdir -p "$WORK/share/doc/kremen/$directory"
    for document in "$SOURCE_ROOT/$directory"/*.md; do
        install -m 644 "$document" "$WORK/share/doc/kremen/$directory/$(basename -- "$document")"
    done
done
for report in "$SOURCE_ROOT/bench"/*.txt; do
    install -m 644 "$report" "$WORK/share/doc/kremen/bench/$(basename -- "$report")"
done
cp -R "$SOURCE_ROOT/examples" "$WORK/share/doc/kremen/examples"
printf '%s\n' kremen-native-install-v1 > "$WORK/share/kremen/install.marker"
(cd "$WORK" && find bin share -type f | LC_ALL=C sort) > "$WORK/manifest"
printf '%s\n' share/kremen/install.manifest >> "$WORK/manifest"
mv "$WORK/manifest" "$WORK/share/kremen/install.manifest"
while IFS= read -r relative; do
    mkdir -p -- "$PREFIX/$(dirname -- "$relative")"
    mode=644
    case "$relative" in bin/kremen|bin/kremen-selfhost) mode=755 ;; esac
    install -m "$mode" "$WORK/$relative" "$PREFIX/$relative"
done < "$WORK/share/kremen/install.manifest"
printf 'Installed Kremen %s in %s\n' "$(cat "$SOURCE_ROOT/VERSION")" "$PREFIX"
printf 'Compiler: %s/bin/kremen\n' "$PREFIX"
printf 'Add this directory to your shell PATH if needed: %s/bin\n' "$PREFIX"
