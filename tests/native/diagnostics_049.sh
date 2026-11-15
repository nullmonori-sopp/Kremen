#!/bin/sh
# Warning policy and machine-readable severity must agree without weakening checks.
set -eu
LC_ALL=C
export LC_ALL
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
KREMEN=${1:-"$ROOT/build/kremen"}
case "$KREMEN" in /*|[A-Za-z]:*) ;; *) KREMEN="$(pwd)/$KREMEN" ;; esac
WORK=$(mktemp -d "${TMPDIR:-/tmp}/kremen-diagnostics049.XXXXXX")
trap 'rm -rf "$WORK"' EXIT HUP INT TERM
EXEEXT=
case "$(uname -s)" in MINGW*|MSYS*) EXEEXT=.exe ;; esac
TOTAL=0
pass() { TOTAL=$((TOTAL + 1)); printf 'ok diagnostics049.%s - %s\n' "$TOTAL" "$1"; }
fail() { printf 'FAIL: %s\n' "$1" >&2; [ ! -f "$WORK/error" ] || cat "$WORK/error" >&2; exit 1; }
contains() { grep -F "$1" "$WORK/error" > /dev/null || fail "missing diagnostic: $1"; }
reject() {
    status=0
    timeout 10 "$KREMEN" "$@" > "$WORK/output" 2> "$WORK/error" || status=$?
    [ "$status" -eq 1 ] || fail "expected diagnostic exit 1, got $status"
    [ ! -s "$WORK/output" ] || fail 'failed compilation wrote to stdout'
    if grep -E 'AddressSanitizer|UndefinedBehaviorSanitizer|runtime error: (load|store|signed integer overflow)' "$WORK/error" > /dev/null; then
        fail 'diagnostic path triggered a sanitizer'
    fi
}
json() {
    reject "$@" --diagnostic-format=json
    "$WORK/json$EXEEXT" "$WORK/error" || fail 'invalid diagnostic JSON'
    contains '"kind":"error"'; contains '"fatal":true'
    contains '"related":'; contains '"imports":'; contains '"message_truncated":'
}
"${CC:-cc}" -std=c11 -Wall -Wextra -Wpedantic -Werror -O2 \
    "$ROOT/tests/native/json_validate.c" -o "$WORK/json$EXEEXT"

printf 'func main() {\n    var unused = 1\n}\n' > "$WORK/input.kr"
reject check "$WORK/input.kr"
contains 'error[W001]: unused variable or parameter'
contains 'note: warnings are treated as errors by default'
contains 'prefix an intentionally unused name with _'
pass 'unused local is fatal and explains the warning policy and remedy'

for mode in work learn; do
    json check "$WORK/input.kr" --mode="$mode"
    contains '"code":"W001"'; contains '"severity":"warning"'
    contains "\"mode\":\"$mode\""; contains '"line":2'
    contains '"column":9'; contains '"end_column":15'; contains '"length":6'
    contains '"explain":"kremen explain W001"'
    pass "$mode warning JSON retains legacy error kind and explicit fatal severity"
done

printf 'func main() {\n    const unused = 1\n}\n' > "$WORK/input.kr"
json check "$WORK/input.kr"
contains '"code":"W001"'; contains '"column":11'; contains '"end_column":17'; contains '"length":6'
pass 'unused const identifies its name instead of the declaration keyword'
printf 'func main() {\n    unused := 1\n}\n' > "$WORK/input.kr"
json check "$WORK/input.kr"
contains '"code":"W001"'; contains '"column":5'; contains '"end_column":11'; contains '"length":6'
pass 'inferred declaration keeps its exact identifier location'

printf 'func main() {\n    var count i64 = "many"\n    println(count)\n}\n' > "$WORK/input.kr"
json check "$WORK/input.kr"
contains '"code":"E110"'; contains '"severity":"error"'
contains '"line":2'; contains '"column":21'; contains '"end_column":27'; contains '"length":6'
pass 'type error keeps its exact half-open byte range and error severity'
reject check "$WORK/input.kr"
contains 'error[E110]'
if grep -F 'warnings are treated as errors' "$WORK/error" > /dev/null; then fail 'ordinary error labeled as a warning'; fi
pass 'ordinary text errors do not claim warning promotion'

printf 'func main() { var count i64; println(count) }\n' > "$WORK/input.kr"
json check "$WORK/input.kr" --mode=learn
contains '"code":"L001"'; contains '"severity":"error"'; contains '"mode":"learn"'
pass 'learning rules remain fatal errors with a distinct diagnostic category'

printf 'func consume(unused i64) {}\nfunc main() { consume(7) }\n' > "$WORK/input.kr"
json check "$WORK/input.kr"
contains '"code":"W001"'; contains '"severity":"warning"'
contains '"line":1'; contains '"column":14'; contains '"end_column":20'; contains '"length":6'
pass 'unused parameter identifies the complete parameter name'

printf 'func main() {\r\n\tvar unused = 1\r\n}\r\n' > "$WORK/input.kr"
json check "$WORK/input.kr"
contains '"code":"W001"'; contains '"line":2'
contains '"column":6'; contains '"end_column":12'; contains '"length":6'
contains '"excerpt":"    var unused = 1"'
pass 'warning excerpts expand tabs and support CRLF sources'

mkdir "$WORK/module"
printf 'func run() { var unused = 1 }\n' > "$WORK/module/item.kr"
printf 'import "module/item"\nfunc main() { item.run() }\n' > "$WORK/input.kr"
json check "$WORK/input.kr"
contains '"code":"W001"'; contains '"imports":[{'; contains 'item.kr'
pass 'fatal warning keeps its imported source and import origin'

printf 'func main() {\n    var value = 1\n    var value = 2\n    println(value)\n}\n' > "$WORK/input.kr"
json check "$WORK/input.kr"
contains '"code":"E108"'; contains '"line":3,"column":5'
contains '"message":"previous declaration is here"'; contains '"line":2,"column":9'
pass 'shadowing keeps its primary location and notes the previous binding name'

printf 'func consume(_unused i64) {}\nfunc main() { var _unused = 1; consume(7) }\n' > "$WORK/input.kr"
for mode in work learn; do
    "$KREMEN" check "$WORK/input.kr" --mode="$mode" --diagnostic-format=json > "$WORK/output" 2> "$WORK/error" || fail "$mode explicit unused binding"
    [ ! -s "$WORK/error" ] || fail "$mode valid program emitted a diagnostic"
done
pass 'explicitly unused locals and parameters remain accepted in both modes'

awk 'BEGIN { printf "func main() { var "; for(i=0;i<5000;i++)printf "a"; print " = 1 }" }' > "$WORK/input.kr"
json check "$WORK/input.kr"
contains '"code":"W001"'; contains '"severity":"warning"'; contains '"message_truncated":true'
[ "$(wc -c < "$WORK/error")" -lt 6000 ] || fail 'warning JSON exceeded bounded message and excerpt budget'
pass 'large warning messages remain bounded, valid JSON and explicitly truncated'
reject check "$WORK/input.kr"
contains 'note: warnings are treated as errors by default'; contains '...'
[ "$(wc -c < "$WORK/error")" -lt 6000 ] || fail 'text warning exceeded bounded message and excerpt budget'
pass 'large text warnings retain the policy note within a bounded output'

printf 'func main() { var unused = 1 }\n' > "$WORK/input.kr"
reject build "$WORK/input.kr" -o "$WORK/rejected$EXEEXT"
contains 'error[W001]'
[ ! -e "$WORK/rejected$EXEEXT" ] || fail 'fatal warning created an executable'
pass 'fatal warning prevents producing an executable'

printf 'func main() { println("\377") }\n' > "$WORK/input.kr"
json check "$WORK/input.kr"
contains '"code":"E100"'; contains '"severity":"error"'; contains '\u00ff'
pass 'invalid source bytes remain escaped in error JSON with severity metadata'

"$KREMEN" explain > "$WORK/output" 2> "$WORK/error" || fail 'diagnostic catalog'
awk '/^[ELW][0-9][0-9][0-9] / { print $1 }' "$WORK/output" > "$WORK/codes"
[ "$(wc -l < "$WORK/codes")" -eq 46 ] || fail 'unexpected diagnostic category count'
while IFS= read -r code; do
    "$KREMEN" explain "$code" > "$WORK/output" 2> "$WORK/error" || fail "missing offline explanation for $code"
    grep -F "$code:" "$WORK/output" > /dev/null || fail "wrong explanation for $code"
done < "$WORK/codes"
"$KREMEN" explain W001 > "$WORK/output" 2> "$WORK/error" || fail 'warning explanation'
grep -F 'severity=warning with fatal=true' "$WORK/output" > /dev/null || fail 'offline explanation omits warning severity policy'
pass 'all 46 categories retain offline explanations including warning severity policy'
printf 'Diagnostic 0.4.9 checks passed: %s.\n' "$TOTAL"
