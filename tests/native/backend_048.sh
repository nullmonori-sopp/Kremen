#!/bin/sh
# Exercise native argv transport and packaging without downloading a real SDK.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
WORK=$(mktemp -d "${TMPDIR:-/tmp}/kremen-backend048.XXXXXX")
trap 'rm -rf "$WORK"' EXIT HUP INT TERM
TOTAL=0
pass() { TOTAL=$((TOTAL + 1)); printf 'ok backend048.%s - %s\n' "$TOTAL" "$1"; }
fail() { printf 'FAIL: %s\n' "$1" >&2; [ ! -f "$WORK/error" ] || cat "$WORK/error" >&2; exit 1; }
mkdir -p "$WORK/backend/bin" "$WORK/backend/lib" "$WORK/build"
COMPILER=${CC:-cc}
if [ "${SANITIZE:-0}" = 1 ]; then
    "$COMPILER" -std=c11 -Wall -Wextra -Wpedantic -Werror -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all "$ROOT/tools/backend_launcher.c" -o "$WORK/backend/bin/clang"
    "$COMPILER" -std=c11 -Wall -Wextra -Wpedantic -Werror -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all "$ROOT/tests/native/backend_048.c" -o "$WORK/backend/zig"
else
    "$COMPILER" -std=c11 -Wall -Wextra -Wpedantic -Werror -O2 "$ROOT/tools/backend_launcher.c" -o "$WORK/backend/bin/clang"
    "$COMPILER" -std=c11 -Wall -Wextra -Wpedantic -Werror -O2 "$ROOT/tests/native/backend_048.c" -o "$WORK/backend/zig"
fi
invoke() { "$1" 'space value' '' 'quote"and\' 'данные/κείμενο' '--literal=;&|$()'; }
invoke "$WORK/backend/bin/clang" > "$WORK/output" 2> "$WORK/error" || fail 'argument transport'
grep -Fx 'argv ok' "$WORK/output" > /dev/null || fail 'argument content'
for mode in -c -fsyntax-only; do
    "$WORK/backend/bin/clang" "$mode" > "$WORK/output" 2> "$WORK/error" || fail 'compile-only transport'
    grep -Fx 'compile-only ok' "$WORK/output" > /dev/null || fail 'link flags in compile-only transport'
done
pass 'backend preserves literal arguments and omits link flags for compile-only commands'
status=0
"$WORK/backend/bin/clang" exit17 > "$WORK/output" 2> "$WORK/error" || status=$?
[ "$status" -eq 17 ] || fail 'backend exit status'
pass 'backend preserves the compiler exit status'
mv "$WORK/backend/zig" "$WORK/zig.saved"
status=0
"$WORK/backend/bin/clang" > "$WORK/output" 2> "$WORK/error" || status=$?
[ "$status" -eq 127 ] && grep -F 'bundled Zig SDK is missing' "$WORK/error" > /dev/null || fail 'missing SDK'
mv "$WORK/zig.saved" "$WORK/backend/zig"
pass 'backend reports a missing SDK without searching PATH'
printf 'SDK license fixture\n' > "$WORK/backend/LICENSE"
printf 'SDK data fixture\n' > "$WORK/backend/lib/payload.h"
cp "$WORK/backend/zig" "$WORK/build/kremen"
cp "$WORK/backend/bin/clang" "$WORK/build/kremen-backend"
sh "$ROOT/tools/package-portable.sh" --backend-root "$WORK/backend" --build-dir "$WORK/build" --output "$WORK/output packages" > "$WORK/log" 2> "$WORK/error" || fail 'offline packaging'
VERSION=$(cat "$ROOT/VERSION")
NAME="kremen-v$VERSION-linux-x86_64"
ARCHIVE="$WORK/output packages/$NAME-portable.tar.gz"
mkdir "$WORK/extracted"
tar -xzf "$ARCHIVE" -C "$WORK/extracted"
DEST="$WORK/extracted/$NAME"
(cd "$DEST" && sha256sum -c SHA256SUMS) > "$WORK/log" 2> "$WORK/error" || fail 'package manifest'
cmp "$WORK/backend/LICENSE" "$DEST/backend/LICENSE" || fail 'SDK license'
cmp "$WORK/backend/lib/payload.h" "$DEST/backend/lib/payload.h" || fail 'SDK content'
[ -f "$DEST/share/kremen/runtime/kremen_rt.h" ] && [ -f "$DEST/share/doc/kremen/examples/hello.kr" ] || fail 'package resources'
pass 'offline package retains SDK licenses resources and a complete checksum manifest'
mv "$DEST" "$WORK/пакет with spaces"
invoke "$WORK/пакет with spaces/backend/bin/clang" > "$WORK/output" 2> "$WORK/error" || fail 'relocated backend'
grep -Fx 'argv ok' "$WORK/output" > /dev/null || fail 'relocated content'
pass 'packaged backend works after relocation into a Unicode path'
PREFIX="$WORK/installed prefix"
mkdir -p "$PREFIX/bin"
printf 'retain\n' > "$PREFIX/bin/unrelated"
sh "$WORK/пакет with spaces/install.sh" --prefix "$PREFIX" > "$WORK/log" 2> "$WORK/error" || fail 'portable install'
cmp "$PREFIX/share/doc/kremen/LICENSE" "$ROOT/LICENSE" || fail 'installed license'
invoke "$PREFIX/backend/bin/clang" > "$WORK/output" 2> "$WORK/error" || fail 'installed backend'
grep -Fx 'retain' "$PREFIX/bin/unrelated" > /dev/null || fail 'unrelated install file'
pass 'prebuilt installer copies backend and licenses while preserving unrelated files'
cp "$PREFIX/share/kremen/portable-install.manifest" "$WORK/manifest"
printf '../outside\n' >> "$PREFIX/share/kremen/portable-install.manifest"
status=0
sh "$WORK/пакет with spaces/install.sh" --prefix "$PREFIX" --uninstall > "$WORK/log" 2> "$WORK/error" || status=$?
[ "$status" -ne 0 ] && [ -x "$PREFIX/bin/kremen" ] || fail 'invalid portable manifest'
cp "$WORK/manifest" "$PREFIX/share/kremen/portable-install.manifest"
pass 'prebuilt uninstall validates its whole manifest before removing files'
sh "$WORK/пакет with spaces/install.sh" --prefix "$PREFIX" > "$WORK/log" 2> "$WORK/error" || fail 'portable upgrade'
sh "$PREFIX/share/kremen/install-portable.sh" --prefix "$PREFIX" --uninstall > "$WORK/log" 2> "$WORK/error" || fail 'portable uninstall'
[ ! -e "$PREFIX/bin/kremen" ] && [ ! -e "$PREFIX/backend/zig" ] || fail 'owned portable files remain'
grep -Fx 'retain' "$PREFIX/bin/unrelated" > /dev/null || fail 'unrelated uninstall file'
sh "$WORK/пакет with spaces/install.sh" --prefix "$PREFIX" > "$WORK/log" 2> "$WORK/error" || fail 'portable reinstall after uninstall'
sh "$WORK/пакет with spaces/install.sh" --prefix "$PREFIX" --uninstall > "$WORK/log" 2> "$WORK/error" || fail 'portable second uninstall'
pass 'prebuilt upgrade and uninstall retain unrelated files'
SUM=$(sha256sum "$ARCHIVE")
status=0
KREMEN_FAKE_SDK_VERSION=0.14.0 sh "$ROOT/tools/package-portable.sh" --backend-root "$WORK/backend" --build-dir "$WORK/build" --output "$WORK/output packages" > "$WORK/output" 2> "$WORK/error" || status=$?
[ "$status" -ne 0 ] && [ "$(sha256sum "$ARCHIVE")" = "$SUM" ] || fail 'wrong SDK preservation'
pass 'a wrong SDK version cannot replace an existing package'
printf 'Backend 0.4.8 checks passed: %s.\n' "$TOTAL"
