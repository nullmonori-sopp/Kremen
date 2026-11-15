#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
DRIVER=${1:-"$ROOT/selfhost/build/kremen-selfhost"}
case "$DRIVER" in /*) ;; *) DRIVER="$(pwd)/$DRIVER" ;; esac
WORK=$(mktemp -d "${TMPDIR:-/tmp}/kremen-subset-output048.XXXXXX")
trap 'rm -rf "$WORK"' EXIT HUP INT TERM
TOTAL=0
pass() { TOTAL=$((TOTAL + 1)); printf 'ok subset-output048.%s - %s\n' "$TOTAL" "$1"; }
fail() { printf 'FAIL: %s\n' "$1" >&2; cat "$WORK/error" >&2; exit 1; }
flags=''
if [ "${SANITIZE:-0}" = 1 ]; then flags='-g -fsanitize=address,undefined -fno-sanitize-recover=all'; fi
# Intentional splitting: flags contains only the fixed instrumentation options above.
"${CC:-cc}" -std=c11 -Wall -Wextra -Wpedantic -Werror -O1 $flags \
    "$ROOT/tests/selfhost/output_048.c" "$ROOT/selfhost/runtime.c" "$ROOT/selfhost/driver.c" -o "$WORK/fixture"
(cd "$WORK" && ./fixture healthy) > "$WORK/output" 2> "$WORK/error" || fail 'healthy output'
printf 'buffered output\n' > "$WORK/expected"
cmp "$WORK/expected" "$WORK/output" || fail 'healthy bytes'
[ ! -s "$WORK/error" ] || fail 'healthy stderr'
pass 'ordinary subset output succeeds unchanged'
for mode in sticky sticky-status; do
    status=0
    (cd "$WORK" && ./fixture "$mode") > "$WORK/output" 2> "$WORK/error" || status=$?
    expected=1
    [ "$mode" != sticky-status ] || expected=7
    [ "$status" -eq "$expected" ] || fail "$mode status=$status"
    grep -Fx 'kremen selfhost: standard output failed' "$WORK/error" > /dev/null || fail "$mode diagnostic"
    pass "$mode detects a sticky stream failure and preserves status"
done
if [ -e /dev/full ]; then
    for command in version check; do
        status=0
        if [ "$command" = version ]; then
            "$DRIVER" --version > /dev/full 2> "$WORK/error" || status=$?
        else
            "$DRIVER" check "$ROOT/selfhost/example.kr" > /dev/full 2> "$WORK/error" || status=$?
        fi
        [ "$status" -eq 1 ] || fail "$command stdout failure status=$status"
        grep -F 'standard output failed' "$WORK/error" > /dev/null || fail "$command output diagnostic"
        pass "$command cannot report success after a failed output write"
    done
    status=0
    (cd "$WORK" && ./fixture large) > /dev/full 2> "$WORK/error" || status=$?
    [ "$status" -eq 1 ] || fail 'large output failure'
    grep -Fx 'kremen selfhost: standard output failed' "$WORK/error" > /dev/null || fail 'large output diagnostic'
    pass 'large output detects an earlier write failure even after flushing'
fi
printf 'Subset output 0.4.8 checks passed: %s.\n' "$TOTAL"
