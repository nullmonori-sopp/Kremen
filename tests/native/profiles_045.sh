#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
KREMEN=${1:-"$ROOT/build/kremen"}
case "$KREMEN" in /*) ;; *) KREMEN="$(pwd)/$KREMEN" ;; esac
WORK=$(mktemp -d "${TMPDIR:-/tmp}/kremen-profiles045.XXXXXX")
trap 'rm -rf "$WORK"' EXIT HUP INT TERM
TOTAL=0
pass() { TOTAL=$((TOTAL + 1)); printf 'ok profiles045.%s - %s\n' "$TOTAL" "$1"; }
fail() { printf 'FAIL: %s\n' "$1" >&2; cat "$WORK/error" >&2; exit 1; }
"${CC:-cc}" -std=c11 -Wall -Wextra -Wpedantic -Werror "$ROOT/tests/native/json_validate.c" -o "$WORK/json"
reject() {
    label=$1; expected=$2; mode=$3; shift 3
    status=0
    timeout 10 "$KREMEN" "$@" --diagnostic-format=json > "$WORK/output" 2> "$WORK/error" || status=$?
    [ "$status" -eq 1 ] || fail "$label status $status"
    [ ! -s "$WORK/output" ] || fail "$label stdout"
    "$WORK/json" "$WORK/error" || fail "$label JSON"
    grep -F "\"code\":\"$expected\"" "$WORK/error" > /dev/null || fail "$label code"
    grep -F "\"mode\":\"$mode\"" "$WORK/error" > /dev/null || fail "$label mode"
    grep -F "\"explain\":\"kremen explain $expected\"" "$WORK/error" > /dev/null || fail "$label explanation"
    pass "$label"
}
learn_reject() {
    label=$1; code=$2
    cat > "$WORK/input.kr"
    "$KREMEN" check "$WORK/input.kr" --mode=work > "$WORK/output" 2> "$WORK/error" || fail "$label must remain valid in work"
    reject "$label" "$code" learn check "$WORK/input.kr" --mode=learn
}
compile() {
    if [ "${SANITIZE_GENERATED:-0}" = 1 ]; then
        "${CC:-cc}" -std=c11 -pthread -Wall -Wextra -Wpedantic -Werror -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all \
            "$1" -o "$2" 2> "$WORK/error" || fail 'sanitized C compilation'
    else
        "${CC:-cc}" -std=c11 -pthread -Wall -Wextra -Wpedantic -Werror "${3:--O3}" "$1" -o "$2" 2> "$WORK/error" || fail 'C compilation'
    fi
}
learn_reject 'learn requires a chosen scalar initializer' L001 <<'KR'
func main() { var count i64; println(count) }
KR
learn_reject 'learn requires an explicit aggregate initializer' L001 <<'KR'
type Pair struct { left i64; right bool }
func main() { var pair Pair; println(pair.left, pair.right) }
KR
learn_reject 'learn requires an explicit array initializer' L001 <<'KR'
func main() { var values [2]i64; println(values[0]) }
KR
learn_reject 'learn rejects direct result discard' L002 <<'KR'
func main() { _ = readIntResult() }
KR
learn_reject 'an underscore prefix does not hide an unread result' L002 <<'KR'
func main() { var _ignored = readIntResult() }
KR
learn_reject 'learn rejects ignored result tuple members' L002 <<'KR'
func pair() (!i64, i64) { return (ok(7), 1) }
func main() { var (_, value) = pair(); println(value) }
KR
learn_reject 'learn rejects unread named result tuple members' L002 <<'KR'
func pair() (!i64, i64) { return (ok(7), 1) }
func main() { var (_ignored, value) = pair(); println(value) }
KR
learn_reject 'an enclosing tuple cannot hide discarded results' L002 <<'KR'
func pair() (!i64, i64) { return (ok(7), 1) }
func main() { _ = pair() }
KR
learn_reject 'an enclosing struct cannot hide discarded results' L002 <<'KR'
type Envelope struct { answer !i64 }
func make() Envelope { return Envelope{answer: ok(7)} }
func main() { _ = make() }
KR
learn_reject 'an array cannot hide discarded results' L002 <<'KR'
func main() { var _ignored = [1]!i64{ok(7)} }
KR
learn_reject 'all enum payload shapes participate in result discard checks' L002 <<'KR'
type Choice enum { Empty; Answer(value !i64) }
func main() { _ = Choice.Empty() }
KR
learn_reject 'learn rejects a silent catch fallback' L003 <<'KR'
func main() { var count = readIntResult() catch 0; println(count) }
KR
grep -F '"column":43' "$WORK/error" > /dev/null || fail 'catch keyword location'
learn_reject 'learn rejects an explicitly ignored catch code' L003 <<'KR'
func main() { var count = readIntResult() catch _ => 0; println(count) }
KR
learn_reject 'learn rejects an unread underscore-prefixed catch code' L003 <<'KR'
func main() { var count = readIntResult() catch _code => 0; println(count) }
KR
learn_reject 'learn requires fail patterns to observe their code' L003 <<'KR'
func main() { var input !i64 = fail(7); match input { ok(value) => { println(value) }; fail(_) => { println("failed") } } }
KR
learn_reject 'learn requires named failure payloads to be used' L003 <<'KR'
func main() { var input !i64 = fail(7); match input { ok(value) => { println(value) }; fail(_code) => { println("failed") } } }
KR
learn_reject 'learn rejects enum wildcard matches' L004 <<'KR'
type Choice enum { Left; Right }
func main() { var choice = Choice.Left(); match choice { Left => { println(1) }; _ => { println(2) } } }
KR
learn_reject 'learn rejects result wildcard matches' L004 <<'KR'
func main() { var input !i64 = ok(3); match input { _ => { println("observed") } } }
KR
learn_reject 'learn selects fallible integer input' L005 <<'KR'
func main() { println(readInt()) }
KR
printf 'func main() { var count i64; println(count) }\n' > "$WORK/input.kr"
"$KREMEN" emit-c "$WORK/input.kr" -o "$WORK/default.c" > "$WORK/output" 2> "$WORK/error" || fail 'default work'
"$KREMEN" emit-c "$WORK/input.kr" --mode=work -o "$WORK/work.c" > "$WORK/output" 2> "$WORK/error" || fail 'explicit work'
cmp "$WORK/default.c" "$WORK/work.c" || fail 'default work output'
compile "$WORK/work.c" "$WORK/program"
"$WORK/program" > "$WORK/output" 2> "$WORK/error" || fail 'zero initialization'
grep -Fx 0 "$WORK/output" > /dev/null || fail 'zero initialization value'
pass 'work is the compatible default and preserves safe zero initialization'

for mode in work learn; do
    "$KREMEN" emit-c "$ROOT/examples/learn/checked_input.kr" --mode="$mode" -o "$WORK/$mode.c" > "$WORK/output" 2> "$WORK/error" || fail "$mode checked input"
done
cmp "$WORK/work.c" "$WORK/learn.c" || fail 'profiles changed lowering'
compile "$WORK/learn.c" "$WORK/program"
printf '4\n' > "$WORK/stdin"
"$WORK/program" < "$WORK/stdin" > "$WORK/output" 2> "$WORK/error" || fail 'positive input'
grep -Fx 25 "$WORK/output" > /dev/null || fail 'positive input answer'
pass 'accepted source generates byte-identical C and handles positive input'
for value in 0 -2; do
    printf '%s\n' "$value" > "$WORK/stdin"
    "$WORK/program" < "$WORK/stdin" > "$WORK/output" 2> "$WORK/error" || fail 'nonpositive input'
    grep -Fx 'Count must be greater than zero.' "$WORK/output" > /dev/null || fail 'nonpositive handling'
done
pass 'the learning example handles zero and negative input as application errors'
printf 'invalid\n' > "$WORK/stdin"
"$WORK/program" < "$WORK/stdin" > "$WORK/output" 2> "$WORK/error" || fail 'malformed input'
grep -F 'Cannot read count:' "$WORK/output" > /dev/null || fail 'malformed handling'
"$WORK/program" < /dev/null > "$WORK/output" 2> "$WORK/error" || fail 'EOF input'
grep -F 'Cannot read count:' "$WORK/output" > /dev/null || fail 'EOF handling'
pass 'the learning example handles malformed input and EOF without panic'

cat > "$WORK/input.kr" <<'KR'
func main() {
    var number = readIntResult() catch code => -code
    var pair = (number, true)
    var (value, _) = pair
    println(value)
}
KR
"$KREMEN" emit-c "$WORK/input.kr" --mode=learn -o "$WORK/positive.c" > "$WORK/output" 2> "$WORK/error" || fail 'used catch code'
compile "$WORK/positive.c" "$WORK/program"
printf '6\n' > "$WORK/stdin"
"$WORK/program" < "$WORK/stdin" > "$WORK/output" 2> "$WORK/error" || fail 'catch success'
grep -Fx 6 "$WORK/output" > /dev/null || fail 'catch success value'
pass 'used catch codes and intentional ordinary-value discard remain accepted'
mkdir "$WORK/module"
printf 'func input() i64 { return readInt() }\n' > "$WORK/module/input.kr"
printf 'import "module/input"\nfunc main() { println(input.input()) }\n' > "$WORK/input.kr"
reject 'learning rules apply through local imports' L005 learn check "$WORK/input.kr" --mode=learn
grep -F '"imports":[{' "$WORK/error" > /dev/null || fail 'learning import origins'
printf 'func main() {}\n' > "$WORK/input.kr"
reject 'unknown modes are rejected' E001 work check "$WORK/input.kr" --mode=fast
reject 'an empty mode is rejected' E001 work check "$WORK/input.kr" --mode=
reject 'a missing mode is rejected' E001 work check "$WORK/input.kr" --mode
reject 'formatting has no semantic profile' E001 work fmt "$WORK/input.kr" --mode=learn
reject 'explanations do not change profile' E001 work explain L001 --mode=learn
printf 'func main() { var number i64; println(number) }\n' > "$WORK/input.kr"
"$KREMEN" check "$WORK/input.kr" --mode=learn --mode=work > "$WORK/output" 2> "$WORK/error" || fail 'last work option'
pass 'the last explicit mode takes precedence'
reject 'a later learn mode activates its requirements' L001 learn check "$WORK/input.kr" --mode=work --mode=learn
reject 'emit-c runs learning checks before generation' L001 learn emit-c "$WORK/input.kr" --mode=learn -o "$WORK/protected.c"
printf 'previous executable\n' > "$WORK/protected"
reject 'build runs learning checks before replacing output' L001 learn build "$WORK/input.kr" --mode=learn -o "$WORK/protected"
grep -Fx 'previous executable' "$WORK/protected" > /dev/null || fail 'failed profile build changed output'
reject 'run checks the selected profile' L001 learn run "$WORK/input.kr" --mode=learn
mkdir "$WORK/dash"
printf 'func main() {}\n' > "$WORK/dash/--mode=learn.kr"
(cd "$WORK/dash" && "$KREMEN" check --mode=learn -- --mode=learn.kr) > "$WORK/output" 2> "$WORK/error" || fail '-- delimiter'
pass '-- preserves a source name beginning with the mode spelling'
for code in L001 L002 L003 L004 L005; do
    "$KREMEN" explain "$code" > "$WORK/output" 2> "$WORK/error" || fail 'learning explanation'
    grep -F 'What to check' "$WORK/output" > /dev/null || fail 'learning remedy'
done
pass 'all learning codes have offline explanations'

cat > "$WORK/input.kr" <<'KR'
func main() { var value = 1; var reference = &mut value; println(*reference); value = 2; println(value) }
KR
for mode in work learn; do reject "$mode retains the same borrowing rules" E131 "$mode" check "$WORK/input.kr" --mode="$mode"; done
cat > "$WORK/input.kr" <<'KR'
func quotient(value i64) i64 { return 100 / value }
func main() { defer println("cleaned"); println(quotient(0)) }
KR
for mode in work learn; do
    "$KREMEN" emit-c "$WORK/input.kr" --mode="$mode" -o "$WORK/runtime.c" > "$WORK/output" 2> "$WORK/error" || fail 'runtime fixture'
    compile "$WORK/runtime.c" "$WORK/program"
    status=0
    "$WORK/program" > "$WORK/output" 2> "$WORK/error" || status=$?
    [ "$status" -eq 1 ] || fail "$mode runtime status"
    grep -F 'runtime error: division by zero' "$WORK/error" > /dev/null || fail "$mode runtime check"
    grep -Fx cleaned "$WORK/output" > /dev/null || fail "$mode defer cleanup"
    pass "$mode retains runtime arithmetic checks and cleanup"
done
printf 'Profile 0.4.5 checks passed: %s.\n' "$TOTAL"
