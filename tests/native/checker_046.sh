#!/bin/sh
# Result-bearing bindings and shared zero-initialization type graphs.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
KREMEN=${1:-"$ROOT/build/kremen"}
case "$KREMEN" in /*) ;; *) KREMEN="$(pwd)/$KREMEN" ;; esac
WORK=$(mktemp -d "${TMPDIR:-/tmp}/kremen-checker046.XXXXXX")
trap 'rm -rf "$WORK"' EXIT HUP INT TERM
TOTAL=0
pass() { TOTAL=$((TOTAL + 1)); printf 'ok checker046.%s - %s\n' "$TOTAL" "$1"; }
fail() { printf 'FAIL: %s\n' "$1" >&2; cat "$WORK/error" >&2; exit 1; }
"${CC:-cc}" -std=c11 -Wall -Wextra -Wpedantic -Werror "$ROOT/tests/native/json_validate.c" -o "$WORK/json"
reject() {
    label=$1; expected=$2; mode=$3; shift 3
    status=0
    timeout 10 "$KREMEN" check "$@" --mode="$mode" --diagnostic-format=json > "$WORK/output" 2> "$WORK/error" || status=$?
    [ "$status" -eq 1 ] || fail "$label status $status"
    [ ! -s "$WORK/output" ] || fail "$label stdout"
    "$WORK/json" "$WORK/error" || fail "$label JSON"
    grep -F "\"code\":\"$expected\"" "$WORK/error" > /dev/null || fail "$label code"
    grep -F "\"mode\":\"$mode\"" "$WORK/error" > /dev/null || fail "$label mode"
    pass "$label"
}
learn_reject() {
    label=$1
    cat > "$WORK/input.kr"
    "$KREMEN" check "$WORK/input.kr" --mode=work > "$WORK/output" 2> "$WORK/error" || fail "$label must remain accepted in work"
    reject "$label" L002 learn "$WORK/input.kr"
}
learn_reject 'enum patterns cannot silently drop a result payload' <<'KR'
type Message enum { Response(answer !i64); Empty }
func main() { var message = Message.Response(fail(7)); match message { Response(_) => { println("response") }; Empty => { println("empty") } } }
KR
learn_reject 'an unread underscore-prefixed enum payload still discards a result' <<'KR'
type Message enum { Response(answer !i64); Empty }
func main() { var message = Message.Response(fail(7)); match message { Response(_answer) => { println("response") }; Empty => { println("empty") } } }
KR
learn_reject 'nested result success patterns cannot discard the inner result' <<'KR'
func nested() !!i64 { return ok(fail(7)) }
func main() { var answer = nested(); match answer { ok(_) => { println("outer success") }; fail(code) => { println(code) } } }
KR
learn_reject 'enum payload structs participate in result discard checks' <<'KR'
type Envelope struct { answer !i64 }
type Message enum { Response(envelope Envelope); Empty }
func main() { var message = Message.Response(Envelope{answer: fail(7)}); match message { Response(_) => { println("response") }; Empty => { println("empty") } } }
KR
learn_reject 'enum payload tuples participate in result discard checks' <<'KR'
type Message enum { Response(pair (!i64, i64)); Empty }
func main() { var message = Message.Response((ok(7), 1)); match message { Response(_) => { println("response") }; Empty => { println("empty") } } }
KR
learn_reject 'enum payload arrays participate in result discard checks' <<'KR'
type Message enum { Response(answers [1]!i64); Empty }
func main() { var message = Message.Response([1]!i64{ok(7)}); match message { Response(_) => { println("response") }; Empty => { println("empty") } } }
KR
learn_reject 'unread result parameters cannot hide behind an underscore prefix' <<'KR'
func ignore(_answer !i64) { println("ignored") }
func main() { var answer !i64 = fail(7); ignore(answer) }
KR
learn_reject 'unread aggregate parameters cannot hide results' <<'KR'
type Envelope struct { answer !i64 }
func ignore(_envelope Envelope) { println("ignored") }
func main() { ignore(Envelope{answer: fail(7)}) }
KR
learn_reject 'unread tuple parameters cannot hide results' <<'KR'
func ignore(_pair (!i64, i64)) { println("ignored") }
func main() { ignore((fail(7), 1)) }
KR
learn_reject 'unread by-value method receivers cannot hide results' <<'KR'
type Envelope struct { answer !i64 }
func (_self Envelope) ignore() { println("ignored") }
func main() { var envelope = Envelope{answer: fail(7)}; envelope.ignore() }
KR

mkdir "$WORK/helper"
cat > "$WORK/helper/input.kr" <<'KR'
func ignore(_answer !i64) { println("ignored") }
KR
cat > "$WORK/input.kr" <<'KR'
import "helper/input"
func main() { var answer !i64 = fail(7); input.ignore(answer) }
KR
"$KREMEN" check "$WORK/input.kr" --mode=work > "$WORK/output" 2> "$WORK/error" || fail 'imported parameter in work'
reject 'result parameter requirements apply to imported bodies' L002 learn "$WORK/input.kr"
grep -F '"imports":[{' "$WORK/error" > /dev/null || fail 'imported parameter origin'
grep -F '"line":1,"column":13' "$WORK/error" > /dev/null || fail 'parameter binding location'

cat > "$WORK/input.kr" <<'KR'
type Envelope struct { answer !i64 }
type Message enum { Response(answer !i64); Empty }
func consume(answer !i64) i64 { return answer catch code => -code }
func (envelope Envelope) value() i64 { return consume(envelope.answer) }
func main() {
    var message = Message.Response(ok(8))
    match message { Response(answer) => { println(consume(answer)) }; Empty => { println(0) } }
    var envelope = Envelope{answer: fail(9)}
    println(envelope.value())
    var nested !!i64 = ok(ok(4))
    match nested { ok(inner) => { println(consume(inner)) }; fail(code) => { println(code) } }
}
KR
for mode in work learn; do
    "$KREMEN" emit-c "$WORK/input.kr" --mode="$mode" -o "$WORK/$mode.c" > "$WORK/output" 2> "$WORK/error" || fail "$mode handled payloads"
done
cmp "$WORK/work.c" "$WORK/learn.c" || fail 'profiles changed lowering'
if [ "${SANITIZE_GENERATED:-0}" = 1 ]; then
    "${CC:-cc}" -std=c11 -pthread -Wall -Wextra -Wpedantic -Werror -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all "$WORK/learn.c" -o "$WORK/program" 2> "$WORK/error" || fail 'sanitized C compilation'
else
    "${CC:-cc}" -std=c11 -pthread -Wall -Wextra -Wpedantic -Werror -O3 "$WORK/learn.c" -o "$WORK/program" 2> "$WORK/error" || fail 'C compilation'
fi
"$WORK/program" > "$WORK/output" 2> "$WORK/error" || fail 'handled payload execution'
printf '8\n-9\n4\n' > "$WORK/expected"
cmp "$WORK/expected" "$WORK/output" || fail 'handled payload output'
pass 'handled parameters, receivers, enum and nested-result payloads retain identical C and output'

cat > "$WORK/input.kr" <<'KR'
type Envelope struct { answer !i64 }
type Message enum { Value(number i64); Empty }
extern func foreign(_answer !i64)
func ignore(_number i64) {}
func reference(_envelope &Envelope) {}
func descriptors(_answers []!i64) {}
func main() {
    var message = Message.Value(7)
    match message { Value(_) => { ignore(1) }; Empty => {} }
    var envelope = Envelope{answer: fail(7)}
    reference(&envelope)
}
KR
for mode in work learn; do
    "$KREMEN" check "$WORK/input.kr" --mode="$mode" > "$WORK/output" 2> "$WORK/error" || fail "$mode ordinary and indirect bindings"
done
pass 'ordinary payload discards, external declarations, references and slice descriptors keep their contract'

# This is check-only: no giant stack-frame program is compiled or executed.
{
    printf 'type Shape0 struct { number i64 }\n'
    i=1
    while [ "$i" -le 17 ]; do previous=$((i - 1)); printf 'type Shape%s struct { left Shape%s; right Shape%s }\n' "$i" "$previous" "$previous"; i=$((i + 1)); done
    printf 'func main() {\n'
    i=0
    while [ "$i" -lt 6000 ]; do printf 'var _value%s Shape17\n' "$i"; i=$((i + 1)); done
    printf '}\n'
} > "$WORK/input.kr"
timeout 10 "$KREMEN" check "$WORK/input.kr" --mode=work > "$WORK/output" 2> "$WORK/error" || fail 'repeated zero-shape graph deadline'
pass 'shared zero-initialization type graphs stay bounded across 6000 local declarations'

cat > "$WORK/input.kr" <<'KR'
type Choice enum { Empty; Answer(value !i64) }
func main() {
    var _first Choice
    var _second Choice
    unsafe { var values = alloc[Choice](1); free(values) }
}
KR
"$KREMEN" check "$WORK/input.kr" --mode=work > "$WORK/output" 2> "$WORK/error" || fail 'empty first variant zero initialization'
pass 'zero initialization uses only an enum first variant, not unreachable payload storage'

for mode in work learn; do
    cat > "$WORK/input.kr" <<'KR'
type Choice enum { Answer(value !i64); Empty }
func main() { var _value Choice }
KR
    reject "$mode retains explicit initialization for first-variant results" E116 "$mode" "$WORK/input.kr"
    cat > "$WORK/input.kr" <<'KR'
type Choice enum { Answer(value !i64); Empty }
func main() { unsafe { var _values = alloc[Choice](1) } }
KR
    reject "$mode retains zero-filled allocation rejection for first-variant results" E130 "$mode" "$WORK/input.kr"
    cat > "$WORK/input.kr" <<'KR'
func main() { var _pair (!i64, i64) }
KR
    reject "$mode retains explicit initialization for result tuples" E116 "$mode" "$WORK/input.kr"
    cat > "$WORK/input.kr" <<'KR'
func main() { var _answers [1]!i64 }
KR
    reject "$mode retains explicit initialization for result arrays" E116 "$mode" "$WORK/input.kr"
done
printf 'Checker 0.4.6 checks passed: %s.\n' "$TOTAL"
