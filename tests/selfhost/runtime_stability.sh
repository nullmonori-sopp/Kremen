#!/bin/sh
# Bounds, immutable-string lifetimes and portable checked arithmetic.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
WORK=$(mktemp -d "${TMPDIR:-/tmp}/kremen-bootstrap-runtime.XXXXXX")
trap 'rm -rf "$WORK"' EXIT HUP INT TERM
COMPILER=${CC:-cc}
TOTAL=0
pass() { TOTAL=$((TOTAL + 1)); printf 'ok bootstrap-runtime.%s - %s\n' "$TOTAL" "$1"; }
# SANITIZE=1 checks this runtime itself without rebuilding shared targets.
if [ "${SANITIZE:-0}" = 1 ]; then
    "$COMPILER" -std=c11 -Wall -Wextra -Wpedantic -Werror -O1 -g \
        -fsanitize=address,undefined -fno-sanitize-recover=all \
        "$ROOT/tests/selfhost/runtime_stability.c" "$ROOT/selfhost/runtime.c" -o "$WORK/runtime"
else
    "$COMPILER" -std=c11 -Wall -Wextra -Wpedantic -Werror -O2 \
        "$ROOT/tests/selfhost/runtime_stability.c" "$ROOT/selfhost/runtime.c" -o "$WORK/runtime"
fi
"$WORK/runtime"
pass 'literal/argv/arena strings, empty/full slices, interleaved lengths, cleanup and arithmetic boundaries'
for mode in addhigh addlow subhigh sublow mulhigh mullow mulneg mulnegboth divzero modzero divmin modmin negmin; do
    if "$WORK/runtime" "$mode" > "$WORK/output" 2> "$WORK/error"; then printf 'FAIL: %s accepted\n' "$mode" >&2; exit 1; fi
    case "$mode" in divzero) diagnostic='division by zero' ;; modzero) diagnostic='remainder by zero' ;; *) diagnostic='integer overflow' ;; esac
    grep -F "checked.kr:17:9: error: $diagnostic" "$WORK/error" > /dev/null || { cat "$WORK/error" >&2; exit 1; }
    if grep -E 'AddressSanitizer|UndefinedBehaviorSanitizer|runtime error:' "$WORK/error" > /dev/null; then cat "$WORK/error" >&2; exit 1; fi
    pass "$mode is diagnosed before unsafe C arithmetic"
done
for mode in slice-negative slice-bounds; do
    if "$WORK/runtime" "$mode" > "$WORK/output" 2> "$WORK/error"; then printf 'FAIL: %s accepted\n' "$mode" >&2; exit 1; fi
    grep -F 'string slice outside bounds' "$WORK/error" > /dev/null || { cat "$WORK/error" >&2; exit 1; }
    pass "$mode"
done
printf 'input data\n' > "$WORK/source.kr"
cp "$WORK/source.kr" "$WORK/original"
ln "$WORK/source.kr" "$WORK/hardlink.kr"
ln -s "$WORK/source.kr" "$WORK/symlink.kr"
for target in "$WORK/source.kr" "$WORK/hardlink.kr" "$WORK/symlink.kr"; do
    if "$WORK/runtime" overwrite "$WORK/source.kr" "$target" > "$WORK/output" 2> "$WORK/error"; then printf 'FAIL: source alias overwritten\n' >&2; exit 1; fi
    grep -F 'output would overwrite a source file' "$WORK/error" > /dev/null || { cat "$WORK/error" >&2; exit 1; }
    cmp "$WORK/source.kr" "$WORK/original"
    pass 'source inode aliases remain protected'
done
"$WORK/runtime" read "$WORK/source.kr"
pass 'source string length is correct after read'
printf '\000bad' > "$WORK/nul.kr"
if "$WORK/runtime" read "$WORK/nul.kr" > "$WORK/output" 2> "$WORK/error"; then printf 'FAIL: source NUL accepted\n' >&2; exit 1; fi
grep -F 'NUL bytes are not allowed' "$WORK/error" > /dev/null
pass 'source NUL rejected'
printf '\n%s bootstrap runtime stability checks passed.\n' "$TOTAL"
