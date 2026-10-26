#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
WORK=$(mktemp -d "${TMPDIR:-/tmp}/kremen-threads-runtime.XXXXXX")
trap 'rm -rf "$WORK"' EXIT HUP INT TERM
CC_TOOL=${CC:-cc}
ASAN_OPTIONS=${ASAN_OPTIONS:-detect_leaks=0}
UBSAN_OPTIONS=${UBSAN_OPTIONS:-halt_on_error=1}
export ASAN_OPTIONS UBSAN_OPTIONS
if [ "${SANITIZE:-0}" = 1 ]; then
    "$CC_TOOL" -std=c11 -Wall -Wextra -Wpedantic -Werror -pthread -O1 -g \
        -fsanitize=address,undefined -fno-sanitize-recover=all "$ROOT/tests/native/threads_runtime.c" -o "$WORK/runtime"
elif [ "${SANITIZE:-0}" = thread ]; then
    "$CC_TOOL" -std=c11 -Wall -Wextra -Wpedantic -Werror -pthread -O1 -g \
        -fsanitize=thread "$ROOT/tests/native/threads_runtime.c" -o "$WORK/runtime"
else
    "$CC_TOOL" -std=c11 -Wall -Wextra -Wpedantic -Werror -pthread -O2 "$ROOT/tests/native/threads_runtime.c" -o "$WORK/runtime"
fi
TOTAL=0
pass() { TOTAL=$((TOTAL + 1)); printf 'ok threads-runtime.%s - %s\n' "$TOTAL" "$1"; }
for mode in overlap nested foreign invalid repeat; do
    timeout 15 "$WORK/runtime" "$mode" > "$WORK/output" 2> "$WORK/error" || { cat "$WORK/error" >&2; exit 1; }
    pass "$mode"
done
printf '9\n' > "$WORK/lock-input"
timeout 15 "$WORK/runtime" locked-panic < "$WORK/lock-input" > "$WORK/output" 2> "$WORK/error" || { cat "$WORK/error" >&2; exit 1; }
grep -Fx 'creator output remains usable' "$WORK/output" > /dev/null
grep -Fx 'worker cleanup' "$WORK/output" > /dev/null
pass 'panic releases worker I/O locks before deferred cleanup and creator input'
timeout 15 "$WORK/runtime" recursive-panic > "$WORK/output" 2> "$WORK/error" || { cat "$WORK/error" >&2; exit 1; }
[ "$(grep -Fc 'worker cleanup' "$WORK/output")" -eq 1 ]
grep -F 'initial failure' "$WORK/error" > /dev/null
grep -F 'cleanup failure' "$WORK/error" > /dev/null
pass 'panic inside deferred cleanup does not rerun an already-popped node'
timeout 20 "$WORK/runtime" resources "$WORK" > "$WORK/output" 2> "$WORK/error" || { cat "$WORK/error" >&2; exit 1; }
for index in 0 1 2 3 4 5 6 7; do [ "$(cat "$WORK/worker-$index")" = owned ]; done
pass '8 workers retain separate allocation, arena, file and string registries'
for mode in panic nested-panic; do
    timeout 15 "$WORK/runtime" "$mode" > "$WORK/output" 2> "$WORK/error" || { cat "$WORK/error" >&2; exit 1; }
    grep -Fx 'worker cleanup' "$WORK/output" > /dev/null
    grep -F 'worker failure' "$WORK/error" > /dev/null
    pass "$mode becomes ECANCELED after worker-local cleanup"
done
for mode in unobserved main-panic deferred-panic; do
    status=0
    timeout 15 "$WORK/runtime" "$mode" > "$WORK/output" 2> "$WORK/error" || status=$?
    [ "$status" -eq 1 ] || { cat "$WORK/error" >&2; printf 'Unexpected %s status: %s\n' "$mode" "$status" >&2; exit 1; }
    grep -F 'runtime error' "$WORK/error" > /dev/null
    if [ "$mode" = main-panic ]; then grep -Fx 'child finished' "$WORK/output" > /dev/null; fi
    if grep -E 'AddressSanitizer|UndefinedBehaviorSanitizer' "$WORK/error" > /dev/null; then cat "$WORK/error" >&2; exit 1; fi
    pass "$mode joins children and exits unsuccessfully"
done
timeout 15 "$WORK/runtime" output > "$WORK/output" 2> "$WORK/error" || { cat "$WORK/error" >&2; exit 1; }
[ "$(wc -l < "$WORK/output")" -eq 640 ]
if grep -Ev '^worker [0-7] iteration [0-9]+$' "$WORK/output"; then exit 1; fi
LC_ALL=C sort "$WORK/output" | uniq > "$WORK/unique"
[ "$(wc -l < "$WORK/unique")" -eq 640 ]
pass 'whole output calls remain intact across 8 workers'
awk 'BEGIN { for(i=1;i<=1000;i++) print i }' > "$WORK/input"
timeout 15 "$WORK/runtime" input < "$WORK/input" > "$WORK/output" 2> "$WORK/error" || { cat "$WORK/error" >&2; exit 1; }
pass 'concurrent readers parse complete integer tokens'
timeout 15 "$WORK/runtime" implicit > "$WORK/output" 2> "$WORK/error" || { cat "$WORK/error" >&2; exit 1; }
grep -Fx 'child finished' "$WORK/output" > /dev/null
pass 'normal process exit waits for an unjoined worker'
printf 'Thread runtime passed: %s checks.\n' "$TOTAL"
