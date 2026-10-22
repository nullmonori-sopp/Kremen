#!/bin/sh
# Compile the teaching examples, check their observable behavior and both input paths.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
KREMEN=${1:-"$ROOT/build/kremen"}
case "$KREMEN" in /*) ;; *) KREMEN="$(pwd)/$KREMEN" ;; esac
WORK=$(mktemp -d "${TMPDIR:-/tmp}/kremen-tutorial.XXXXXX")
trap 'rm -rf "$WORK"' EXIT HUP INT TERM
cd "$WORK"
TOTAL=0
check() {
    lesson=$1; expected=$2
    "$KREMEN" build "$ROOT/examples/tutorial/$lesson.kr" -o "$WORK/program" > "$WORK/build.log" 2>&1 || { cat "$WORK/build.log" >&2; exit 1; }
    "$WORK/program" < "$WORK/input" > "$WORK/actual" 2> "$WORK/error" || { cat "$WORK/error" >&2; exit 1; }
    printf '%s\n' "$expected" > "$WORK/expected"
    cmp "$WORK/expected" "$WORK/actual"
    TOTAL=$((TOTAL + 1)); printf 'ok tutorial.%s - %s\n' "$TOTAL" "$lesson"
}
printf '17\n' > "$WORK/input"
check 01_hello 'Hello, Kremen!'
check 02_variables 'apples: 5
cost: 20'
check 03_control_flow 'even: 2
even: 4
total: 15'
check 04_functions '42'
check 05_records_arrays 'total: 12
original: 2 copy: 9'
check 06_input 'Enter an integer:
Next: 18'
printf 'no\n' > "$WORK/input"
check 06_input 'Enter an integer:
Input error: 2'
check 07_results '41
7'
check 08_enums_tuples 'no reading
reading: 42
7 seven'
check 09_memory 'number: 2
slice: 3 42
arena: 9'
check 10_files_cleanup 'saved bytes: 6
finished'
printf 'hello\n' > "$WORK/expected-file"
cmp "$WORK/expected-file" "$WORK/kremen-tutorial.txt"
printf 'Tutorial passed: %s executions and file contents.\n' "$TOTAL"
