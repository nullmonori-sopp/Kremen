#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
KREMEN=${1:-"$ROOT/build/kremen"}
case "$KREMEN" in /*) ;; *) KREMEN="$(pwd)/$KREMEN" ;; esac
WORK=$(mktemp -d "${TMPDIR:-/tmp}/kremen-output046.XXXXXX")
trap 'rm -rf "$WORK"' EXIT HUP INT TERM
TOTAL=0
pass() { TOTAL=$((TOTAL + 1)); printf 'ok output046.%s - %s\n' "$TOTAL" "$1"; }
fail() { printf 'FAIL: %s\n' "$1" >&2; cat "$WORK/error" >&2; exit 1; }
"${CC:-cc}" -std=c11 -Wall -Wextra -Wpedantic -Werror "$ROOT/tests/native/json_validate.c" -o "$WORK/json"
"${CC:-cc}" -std=c11 -Wall -Wextra -Wpedantic -Werror "$ROOT/tests/native/output_failure_046.c" -o "$WORK/limit"
printf 'func main() { println(7) }\n' > "$WORK/main.kr"
printf 'previous artifact\n' > "$WORK/blocked"
mkdir "$WORK/directory"
reject() {
    label=$1; shift
    status=0
    "$@" --diagnostic-format=json > "$WORK/output" 2> "$WORK/error" || status=$?
    [ "$status" -eq 1 ] || fail "$label status=$status"
    [ ! -s "$WORK/output" ] || fail "$label stdout"
    "$WORK/json" "$WORK/error" || fail "$label JSON"
    grep -F '"code":"E015"' "$WORK/error" > /dev/null || fail "$label code"
    grep -F '"explain":"kremen explain E015"' "$WORK/error" > /dev/null || fail "$label explanation"
    pass "$label"
}
reject 'blocked parent is structured' "$KREMEN" emit-c "$WORK/main.kr" -o "$WORK/blocked/child.c"
grep -Fx 'previous artifact' "$WORK/blocked" > /dev/null || fail 'blocked parent changed'
reject 'directory cannot be replaced by emitted C' "$KREMEN" emit-c "$WORK/main.kr" -o "$WORK/directory"
reject 'backend staging parent failure is structured' "$KREMEN" build "$WORK/main.kr" -o "$WORK/blocked/program"
reject 'backend destination replacement is structured' "$KREMEN" build "$WORK/main.kr" -o "$WORK/directory"
ESC=$(printf '\033')
reject 'output path controls remain escaped in JSON' "$KREMEN" emit-c "$WORK/main.kr" -o "$WORK/blocked/$ESC.c"
grep -F '\u001b' "$WORK/error" > /dev/null || fail 'escaped output path'
status=0
"$KREMEN" emit-c "$WORK/main.kr" -o "$WORK/blocked/$ESC.c" > "$WORK/output" 2> "$WORK/error" || status=$?
[ "$status" -eq 1 ] || fail 'text failure status'
if LC_ALL=C grep "$ESC" "$WORK/error" > /dev/null; then fail 'raw terminal escape'; fi
grep -F '\x1b' "$WORK/error" > /dev/null || fail 'text path escape'
pass 'text output failures escape terminal controls'
printf 'previous artifact\n' > "$WORK/existing.c"
reject 'failed staged write preserves previous output' "$WORK/limit" "$KREMEN" emit-c "$WORK/main.kr" -o "$WORK/existing.c"
grep -Fx 'previous artifact' "$WORK/existing.c" > /dev/null || fail 'existing output replaced'
reject 'failed backend source write is structured' "$WORK/limit" "$KREMEN" build "$WORK/main.kr" -o "$WORK/program"
cp "$WORK/main.kr" "$WORK/format.kr"
cp "$WORK/format.kr" "$WORK/original.kr"
reject 'failed formatter write preserves source' "$WORK/limit" "$KREMEN" fmt -w "$WORK/format.kr"
cmp "$WORK/format.kr" "$WORK/original.kr" || fail 'formatter changed source'
leftovers=$(find "$WORK" -name '*.kremen-*' -print)
[ -z "$leftovers" ] || fail 'staging residue'
pass 'all failed file operations remove staging artifacts'
"$KREMEN" emit-c "$WORK/main.kr" -o "$WORK/nested/path/main.c" > "$WORK/output" 2> "$WORK/error" || fail 'nested output'
[ -s "$WORK/nested/path/main.c" ] || fail 'empty output'
pass 'nested successful output still creates parent directories'
chmod 640 "$WORK/format.kr"
"$KREMEN" fmt -w "$WORK/format.kr" > "$WORK/output" 2> "$WORK/error" || fail 'formatter success'
[ "$(stat -c %a "$WORK/format.kr")" = 640 ] || fail 'formatter permissions'
pass 'successful formatter replacement preserves permissions'
if [ -e /dev/full ]; then
    for command in check emit-c; do
        status=0
        "$KREMEN" "$command" "$WORK/main.kr" --diagnostic-format=json > /dev/full 2> "$WORK/error" || status=$?
        [ "$status" -eq 1 ] || fail "$command stdout failure status"
        "$WORK/json" "$WORK/error" || fail "$command stdout JSON"
        grep -F '"code":"E015"' "$WORK/error" > /dev/null || fail "$command stdout code"
        pass "$command detects buffered stdout failure"
    done
fi
printf 'Output 0.4.6 checks passed: %s.\n' "$TOTAL"
