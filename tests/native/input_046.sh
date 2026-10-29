#!/bin/sh
# Input-result codes are not OS errno values: verify the teaching example's
# messages for successful, application-error, and input-error paths.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
KREMEN=${1:-"$ROOT/build/kremen"}
case "$KREMEN" in /*) ;; *) KREMEN="$(pwd)/$KREMEN" ;; esac
WORK=$(mktemp -d "${TMPDIR:-/tmp}/kremen-input046.XXXXXX")
trap 'rm -rf "$WORK"' EXIT HUP INT TERM
TOTAL=0
fail() { printf 'FAIL: %s\n' "$1" >&2; cat "$WORK/error" >&2; exit 1; }
run_case() {
    label=$1; input=$2; expected=$3
    printf '%s' "$input" > "$WORK/input"
    timeout 10 "$WORK/program" < "$WORK/input" > "$WORK/output" 2> "$WORK/error" || fail "$mode $label status"
    [ ! -s "$WORK/error" ] || fail "$mode $label stderr"
    printf '%s\n' "$expected" > "$WORK/expected"
    cmp "$WORK/expected" "$WORK/output" || fail "$mode $label message"
    TOTAL=$((TOTAL + 1)); printf 'ok input046.%s - %s: %s\n' "$TOTAL" "$mode" "$label"
}
for mode in work learn; do
    "$KREMEN" emit-c "$ROOT/examples/learn/checked_input.kr" --mode="$mode" -o "$WORK/program.c" > "$WORK/output" 2> "$WORK/error" || fail "$mode emit-c"
    if [ "${SANITIZE_GENERATED:-0}" = 1 ]; then
        "${CC:-cc}" -std=c11 -pthread -Wall -Wextra -Wpedantic -Werror -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all \
            "$WORK/program.c" -o "$WORK/program" 2> "$WORK/error" || fail "$mode sanitized C compilation"
    else
        "${CC:-cc}" -std=c11 -pthread -Wall -Wextra -Wpedantic -Werror -O3 \
            "$WORK/program.c" -o "$WORK/program" 2> "$WORK/error" || fail "$mode C compilation"
    fi
    run_case 'positive integer' '4' '25'
    run_case 'zero application error' '0' 'Count must be greater than zero.'
    run_case 'negative application error' '-2' 'Count must be greater than zero.'
    run_case 'EOF has its own input message' '' 'Cannot read count: end of input.'
    run_case 'malformed input has its own input message' 'oops' 'Cannot read count: expected an integer.'
    run_case 'positive overflow has its own input message' '9223372036854775808' 'Cannot read count: integer is outside the i64 range.'
    run_case 'negative overflow has its own input message' '-9223372036854775809' 'Cannot read count: integer is outside the i64 range.'
    timeout 10 "$WORK/program" <&- > "$WORK/output" 2> "$WORK/error" || fail "$mode stream error status"
    [ ! -s "$WORK/error" ] || fail "$mode stream error stderr"
    printf 'Cannot read count: input stream failed.\n' > "$WORK/expected"
    cmp "$WORK/expected" "$WORK/output" || fail "$mode stream error message"
    TOTAL=$((TOTAL + 1)); printf 'ok input046.%s - %s: closed stdin reports an input stream error\n' "$TOTAL" "$mode"
done
printf 'Input 0.4.6 checks passed: %s.\n' "$TOTAL"
