#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
KREMEN=${1:-"$ROOT/build/kremen"}
case "$KREMEN" in /*) ;; *) KREMEN="$(pwd)/$KREMEN" ;; esac
WORK=$(mktemp -d "${TMPDIR:-/tmp}/kremen-diagnostics-043.XXXXXX")
trap 'rm -rf "$WORK"' EXIT HUP INT TERM
"${CC:-cc}" -std=c11 -Wall -Wextra -Wpedantic -Werror -O2 "$ROOT/tests/native/json_validate.c" -o "$WORK/json"
TOTAL=0
pass() { TOTAL=$((TOTAL + 1)); printf 'ok diagnostics043.%s - %s\n' "$TOTAL" "$1"; }
fail() { printf 'FAIL: %s\n' "$1" >&2; cat "$WORK/error" >&2; exit 1; }
contains() { grep -F "$1" "$WORK/error" > /dev/null || fail "missing $1"; }
reject() {
    status=0
    "$KREMEN" "$@" > "$WORK/output" 2> "$WORK/error" || status=$?
    [ "$status" -eq 1 ] || fail "expected diagnostic status1, got$status"
    if grep -E 'AddressSanitizer|UndefinedBehaviorSanitizer' "$WORK/error" > /dev/null; then fail 'unsafe diagnostic'; fi
}
json() { reject "$@" --diagnostic-format=json; "$WORK/json" "$WORK/error" || fail 'invalid JSON'; }
printf 'func main() {\n    var total i64 = "many"\n    println(total)\n}\n' > "$WORK/input.kr"
reject check "$WORK/input.kr"
contains '1 | func main() {'; contains '2 |'; contains '3 |     println(total)'; contains '^~~~~~'
pass 'text diagnostic includes surrounding source context'
json check "$WORK/input.kr"
contains '"kind":"error"'; contains '"code":"E110"'; contains '"line":2'; contains '"column":21'; contains '"end_column":27'
contains '"related":[]'; contains '"imports":[]'; contains '"hint":"Check the value'
pass 'JSON type diagnostic has precise half-open range and hint'
printf 'func main() {\nvar value = 1\nvar reference = &mut value\nprintln(*reference)\nvalue = 2\n}\n' > "$WORK/input.kr"
json check "$WORK/input.kr"
contains '"related":[{"message":"conflicting borrow starts here"'; contains '"line":3'
pass 'JSON preserves the source of a conflicting borrow'
printf 'func main() { var count = 1; var count = 2; println(count) }\n' > "$WORK/input.kr"
json check "$WORK/input.kr"; contains 'previous declaration is here'
pass 'JSON local-shadow diagnostic has related declaration'
mkdir -p "$WORK/sub/deep"
printf 'import "sub/module"\nfunc main() { module.run() }\n' > "$WORK/main.kr"
printf 'import "deep/fault"\nfunc run() { fault.run() }\n' > "$WORK/sub/module.kr"
printf 'func run() { println(unknown) }\n' > "$WORK/sub/deep/fault.kr"
json check "$WORK/main.kr"
contains '"imports":[{'; contains '/sub/module.kr'; contains '/main.kr'; contains '/sub/deep/fault.kr'
pass 'nested imports retain the complete diagnostic origin chain'
reject check "$WORK/main.kr"
[ "$(grep -c 'note: imported from here' "$WORK/error")" -eq 2 ] || fail 'lost import origin'
pass 'text imports identify both importing locations'
printf 'func main() {\r\n    println(1)\r\n    \000\r\n}\r\n' > "$WORK/input.kr"
json check "$WORK/input.kr"; contains '"line":3'; contains '"column":5'; contains 'NUL byte'
pass 'NUL bytes report their exact CRLF source location'
printf 'func main() { println("\377") }\n' > "$WORK/input.kr"
json check "$WORK/input.kr"; contains 'Invalid UTF-8'; contains '\u00ff'
pass 'malformed UTF-8 produces valid escaped JSON'
mkdir "$WORK/quote\"name"
printf 'func main() { println(missing) }\n' > "$WORK/quote\"name/input.kr"
json check "$WORK/quote\"name/input.kr"; contains 'quote\"name'
pass 'JSON escapes quotes in file paths'
awk 'BEGIN { printf "func main() { println("; for(i=0;i<5000;i++) printf "a"; print ") }" }' > "$WORK/input.kr"
json check "$WORK/input.kr"; contains '"message_truncated":true'; contains '"length":5000'
pass 'long messages are explicitly truncated while source ranges stay exact'
printf 'func main() {\n' > "$WORK/input.kr"
json check "$WORK/input.kr"; contains '"line":2'; contains '"column":1'; contains '"length":0'
pass 'EOF diagnostic has a valid empty range'
json check "$WORK/missing.kr"; contains '"code":"E010"'; contains 'missing.kr'
pass 'missing input path is reported in structured form'
reject check "$WORK/input.kr" --diagnostic-format=json --diagnostic-format=bad
"$WORK/json" "$WORK/error" || fail 'invalid option JSON'; contains '"code":"E001"'
pass 'invalid diagnostic format is a structured command-line error'
printf 'func main() { println(42) }\n' > "$WORK/input.kr"
"$KREMEN" check "$WORK/input.kr" --diagnostic-format=json > "$WORK/output" 2> "$WORK/error"
[ ! -s "$WORK/error" ] || fail 'success emitted a diagnostic'
pass 'valid programs have an empty diagnostic stream'
printf 'Diagnostic 0.4.3 checks passed: %s.\n' "$TOTAL"
