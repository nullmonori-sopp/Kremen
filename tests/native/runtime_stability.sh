#!/bin/sh
# Full runtime invariants plus bounded file-read memory use. No Python.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
WORK=$(mktemp -d "${TMPDIR:-/tmp}/kremen-runtime-stability.XXXXXX")
trap 'rm -rf "$WORK"' EXIT HUP INT TERM
COMPILER=${CC:-cc}
if [ "${SANITIZE:-0}" = 1 ]; then
    "$COMPILER" -std=c11 -Wall -Wextra -Wpedantic -Werror -O1 -g \
        -fsanitize=address,undefined -fno-sanitize-recover=all \
        "$ROOT/tests/native/runtime_stability.c" -o "$WORK/runtime"
else
    "$COMPILER" -std=c11 -Wall -Wextra -Wpedantic -Werror -O2 \
        "$ROOT/tests/native/runtime_stability.c" -o "$WORK/runtime"
fi
TOTAL=0
for mode in arithmetic arena line-empty line-crlf line-4096 stream-0 stream-1 stream-4095 stream-4096 stream-4097 stream-8192 stream-8193 stream-67108864 stream-67108865 stream-error; do
    "$WORK/runtime" "$mode" > "$WORK/output" 2> "$WORK/error" || { cat "$WORK/error" >&2; exit 1; }
    TOTAL=$((TOTAL + 1)); printf 'ok runtime.%s - %s\n' "$TOTAL" "$mode"
done
printf 'defer 2\ndefer 1\n' > "$WORK/expected"
for entry in 'overflow-mul|integer overflow' 'overflow-add|integer overflow' 'overflow-sub|integer overflow' \
    'overflow-shift|integer overflow' 'zero-divide|division by zero' 'invalid-cast|integer conversion out of range' 'arena-after-reset|slice use after free'; do
    mode=${entry%%|*}; reason=${entry#*|}
    if "$WORK/runtime" "$mode" > "$WORK/output" 2> "$WORK/error"; then printf 'FAIL: %s accepted\n' "$mode" >&2; exit 1; fi
    grep -F "$reason" "$WORK/error" > /dev/null || { cat "$WORK/error" >&2; exit 1; }
    cmp "$WORK/expected" "$WORK/output"
    if grep -E 'AddressSanitizer|UndefinedBehaviorSanitizer|runtime error:.*(misaligned|overflow)' "$WORK/error" > /dev/null; then cat "$WORK/error" >&2; exit 1; fi
    TOTAL=$((TOTAL + 1)); printf 'ok runtime.%s - %s with LIFO cleanup\n' "$TOTAL" "$mode"
done
printf '\n%s full runtime stability checks passed.\n' "$TOTAL"
