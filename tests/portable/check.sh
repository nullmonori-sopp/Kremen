#!/bin/sh
# Real SDK checks: this target intentionally does compile and run programs.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
[ "$#" -eq 1 ] || { printf '%s\n' 'Usage: sh tests/portable/check.sh /unpacked/package'; exit 2; }
PACKAGE_ROOT=$(CDPATH= cd -- "$1" && pwd)
WORK=$(mktemp -d "${TMPDIR:-/tmp}/kremen-portable048.XXXXXX")
trap 'chmod -R u+w "$WORK" 2>/dev/null || :; rm -rf "$WORK"' EXIT HUP INT TERM
mkdir "$WORK/no developer tools"
EMPTY_PATH="$WORK/no developer tools"
TOTAL=0
pass() { TOTAL=$((TOTAL + 1)); printf 'ok portable048.%s - %s\n' "$TOTAL" "$1"; }
fail() { printf 'FAIL: %s\n' "$1" >&2; [ ! -f "$WORK/error" ] || cat "$WORK/error" >&2; exit 1; }
(cd "$PACKAGE_ROOT" && sha256sum -c SHA256SUMS) > "$WORK/manifest.log" 2> "$WORK/error" || fail 'package checksums'
pass 'complete portable archive checksum manifest'
mv "$PACKAGE_ROOT" "$WORK/пакет Ω with spaces"
PACKAGE_ROOT="$WORK/пакет Ω with spaces"
KREMEN="$PACKAGE_ROOT/bin/kremen"
BACKEND="$PACKAGE_ROOT/backend/bin/clang"
invoke() {
    status=0
    env -u CC -u KREMEN_SELFHOST_ROOT PATH="$EMPTY_PATH" "$KREMEN" "$@" > "$WORK/output" 2> "$WORK/error" || status=$?
    [ "$status" -eq 0 ] || fail "portable command status=$status: $*"
}
invoke doctor
grep -Fx 'Worker spawn/join: OK' "$WORK/output" > /dev/null || fail 'doctor worker'
grep -F "$PACKAGE_ROOT/backend/bin/clang" "$WORK/output" > /dev/null || fail 'bundled backend selection'
pass 'Unicode relocation and doctor without system compiler or PATH tools'
mkdir "$WORK/источники"
cp "$ROOT/tests/windows/runtime048.kr" "$WORK/источники/задача 🌍.kr"
for mode in work learn; do
    (cd "$WORK" && invoke run "$WORK/источники/задача 🌍.kr" --mode="$mode")
    grep -Fx 'native runtime 048 ok' "$WORK/output" > /dev/null || fail "$mode runtime output"
    pass "$mode Unicode filesystem and native worker runtime"
done
invoke emit-c "$WORK/источники/задача 🌍.kr" -o "$WORK/generated.c"
env -u CC PATH="$EMPTY_PATH" "$BACKEND" -std=c11 -Wall -Wextra -Wpedantic -Werror -pthread "$WORK/generated.c" -o "$WORK/program" > "$WORK/output" 2> "$WORK/error" || fail 'standalone C compilation'
(cd "$WORK" && env PATH="$EMPTY_PATH" "$WORK/program") > "$WORK/output" 2> "$WORK/error" || fail 'standalone program'
grep -Fx 'native runtime 048 ok' "$WORK/output" > /dev/null || fail 'standalone program output'
pass 'emitted standalone C compiles with only the bundled backend'
for binary in "$KREMEN" "$PACKAGE_ROOT/bin/kremen-selfhost" "$BACKEND" "$WORK/program"; do
    readelf -l "$binary" > "$WORK/elf" 2> "$WORK/error" || fail 'ELF inspection'
    if grep -F INTERP "$WORK/elf" > /dev/null; then fail "dynamic interpreter dependency: $binary"; fi
done
pass 'frontend subset launcher and generated program are statically linked'
status=0
env CC="$WORK/missing compiler" PATH="$EMPTY_PATH" "$KREMEN" doctor --diagnostic-format=json > "$WORK/output" 2> "$WORK/error" || status=$?
[ "$status" -eq 1 ] && grep -F '"code":"E016"' "$WORK/error" > /dev/null || fail 'CC override'
pass 'an explicit CC override retains precedence over the bundled backend'
printf 'func main() i64 { return 7 }\n' > "$WORK/subset.kr"
invoke selfhost check "$WORK/subset.kr"
status=0
env -u CC -u KREMEN_SELFHOST_ROOT PATH="$EMPTY_PATH" "$KREMEN" selfhost run "$WORK/subset.kr" > "$WORK/output" 2> "$WORK/error" || status=$?
[ "$status" -eq 7 ] || fail 'subset run status'
pass 'installed native subset compiles and executes through the bundled backend'
REBUILT="$PACKAGE_ROOT/bin/rebuilt-selfhost"
invoke selfhost bootstrap -o "$REBUILT"
env -u CC -u KREMEN_SELFHOST_ROOT PATH="$EMPTY_PATH" "$REBUILT" check "$WORK/subset.kr" > "$WORK/output" 2> "$WORK/error" || fail 'rebuilt subset'
rm "$REBUILT"
pass 'bundled backend reproduces and runs the subset compiler and driver'
chmod -R a-w "$PACKAGE_ROOT"
ZIG_GLOBAL_CACHE_DIR="$WORK/readonly cold cache" invoke doctor
chmod -R u+w "$PACKAGE_ROOT"
pass 'ordinary compilation needs no writes inside the portable installation'
PREFIX="$WORK/installed prefix"
mkdir -p "$PREFIX/bin"
printf 'retain\n' > "$PREFIX/bin/unrelated"
sh "$PACKAGE_ROOT/install.sh" --prefix "$PREFIX" > "$WORK/install.log" 2> "$WORK/error" || fail 'prebuilt installation'
env -u CC -u KREMEN_SELFHOST_ROOT PATH="$EMPTY_PATH" "$PREFIX/bin/kremen" doctor > "$WORK/output" 2> "$WORK/error" || fail 'installed portable doctor'
pass 'prebuilt installation retains a working bundled backend'
sh "$PREFIX/share/kremen/install-portable.sh" --prefix "$PREFIX" --uninstall > "$WORK/install.log" 2> "$WORK/error" || fail 'prebuilt uninstall'
[ ! -e "$PREFIX/bin/kremen" ] && [ ! -e "$PREFIX/backend/zig" ] || fail 'owned installation remains'
grep -Fx retain "$PREFIX/bin/unrelated" > /dev/null || fail 'unrelated file removed'
pass 'prebuilt uninstall removes owned files and preserves unrelated data'
printf 'Portable Linux 0.4.8 checks passed: %s.\n' "$TOTAL"
