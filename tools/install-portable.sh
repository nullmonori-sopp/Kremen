#!/bin/sh
# Install prebuilt portable files; no compiler, SDK download, or root required.
set -eu
umask 022
SOURCE_ROOT=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
PREFIX=${HOME:+"$HOME/.local"}
ACTION=install
while [ "$#" -gt 0 ]; do
    case "$1" in
        --prefix) [ "$#" -ge 2 ] || exit 2; PREFIX=$2; shift 2 ;;
        --uninstall) ACTION=uninstall; shift ;;
        --help|-h) printf '%s\n' 'Usage: sh install.sh [--prefix /absolute/path] [--uninstall]'; exit 0 ;;
        *) printf 'Unknown option: %s\n' "$1" >&2; exit 2 ;;
    esac
done
while [ "${PREFIX%/}" != "$PREFIX" ]; do PREFIX=${PREFIX%/}; done
case "$PREFIX" in /*) ;; *) printf '%s\n' 'Choose an absolute installation directory other than /.' >&2; exit 2 ;; esac
case "$PREFIX" in *'
'*|*/../*|*/..|*/./*|*/.) printf '%s\n' 'Unsupported installation prefix.' >&2; exit 2 ;; esac
MARKER="$PREFIX/share/kremen/portable-install.marker"
MANIFEST="$PREFIX/share/kremen/portable-install.manifest"
OWNED=false
if [ -f "$MARKER" ] && [ -f "$MANIFEST" ] && [ "$(cat "$MARKER")" = kremen-portable-install-v1 ]; then OWNED=true; fi
valid_path() {
    case "$1" in ''|/*|../*|*/../*|*/..|./*|*/./*|*/.|*//*|*\\*) return 1 ;; esac
    case "$1" in bin/kremen|bin/kremen-selfhost|backend/*|share/kremen/runtime/kremen_rt.h|share/kremen/native/platform.h|share/kremen/selfhost/*|share/kremen/install-portable.sh|share/kremen/portable-install.marker|share/kremen/portable-install.manifest|share/doc/kremen/*) return 0 ;; *) return 1 ;; esac
}
# A leaf symlink is safe to remove, but no operation may traverse a symlink
# within the prefix to write or remove a file outside this installation.
check_parents() {
    parent_path=$PREFIX
    [ ! -L "$parent_path" ] || return 1
    remaining=$1
    while case "$remaining" in */*) true ;; *) false ;; esac; do
        component=${remaining%%/*}; remaining=${remaining#*/}
        parent_path="$parent_path/$component"
        [ ! -L "$parent_path" ] || return 1
    done
}
validate_manifest() {
    while IFS= read -r relative; do
        valid_path "$relative" && check_parents "$relative" || return 1
    done < "$MANIFEST"
}
if [ "$OWNED" = true ]; then validate_manifest || { printf '%s\n' 'Invalid portable manifest; no files changed.' >&2; exit 1; }; fi
if [ "$ACTION" = uninstall ]; then
    [ "$OWNED" = true ] || { printf '%s\n' 'No owned portable installation at this prefix.' >&2; exit 1; }
    while IFS= read -r relative; do
        [ "$relative" = share/kremen/portable-install.manifest ] || rm -f -- "$PREFIX/$relative"
    done < "$MANIFEST"
    rm -f -- "$MANIFEST"
    find "$PREFIX/backend" "$PREFIX/share/kremen" "$PREFIX/share/doc/kremen" -depth -type d -exec rmdir -- {} \; 2>/dev/null || :
    printf 'Removed owned Kremen files from %s\n' "$PREFIX"
    exit 0
fi
[ -x "$SOURCE_ROOT/bin/kremen" ] && [ -x "$SOURCE_ROOT/backend/zig" ] || { printf '%s\n' 'Run this installer from a complete unpacked portable package.' >&2; exit 1; }
if [ "$OWNED" != true ]; then
    for reserved in bin/kremen bin/kremen-selfhost share/kremen share/doc/kremen backend; do
        [ ! -e "$PREFIX/$reserved" ] && [ ! -L "$PREFIX/$reserved" ] || { printf 'Refusing unowned path: %s\n' "$PREFIX/$reserved" >&2; exit 1; }
    done
fi
WORK=$(mktemp -d "${TMPDIR:-/tmp}/kremen-portable-install.XXXXXX")
trap 'rm -rf "$WORK"' EXIT HUP INT TERM
for directory in bin share backend; do cp -a "$SOURCE_ROOT/$directory" "$WORK/$directory"; done
mkdir -p "$WORK/share/kremen" "$WORK/share/doc/kremen"
cp "$SOURCE_ROOT/LICENSE" "$WORK/share/doc/kremen/LICENSE"
cp "$0" "$WORK/share/kremen/install-portable.sh"
printf '%s\n' kremen-portable-install-v1 > "$WORK/share/kremen/portable-install.marker"
rm -f "$WORK/share/kremen/portable-install.manifest"
(cd "$WORK" && find bin share backend \( -type f -o -type l \) | LC_ALL=C sort) > "$WORK/manifest"
printf '%s\n' share/kremen/portable-install.manifest >> "$WORK/manifest"
mv "$WORK/manifest" "$WORK/share/kremen/portable-install.manifest"
while IFS= read -r relative; do
    valid_path "$relative" && check_parents "$relative" || { printf 'Unsafe installation path: %s\n' "$relative" >&2; exit 1; }
    [ ! -L "$PREFIX/$relative" ] || { printf 'Refusing to overwrite symlink: %s\n' "$relative" >&2; exit 1; }
done < "$WORK/share/kremen/portable-install.manifest"
while IFS= read -r relative; do
    mkdir -p -- "$PREFIX/$(dirname -- "$relative")"
    cp -P --preserve=mode,timestamps "$WORK/$relative" "$PREFIX/$relative"
done < "$WORK/share/kremen/portable-install.manifest"
printf 'Installed portable Kremen in %s\n' "$PREFIX"
