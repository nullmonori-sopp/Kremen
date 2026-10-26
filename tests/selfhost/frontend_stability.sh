#!/bin/sh
# Bootstrap order/overflow regressions, exercised independently of the C backend.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
COMPILER=${1:-"$ROOT/selfhost/build/stage2"}
case "$COMPILER" in /*) ;; *) COMPILER="$(pwd)/$COMPILER" ;; esac
WORK=$(mktemp -d "${TMPDIR:-/tmp}/kremen-bootstrap-stability.XXXXXX")
trap 'rm -rf "$WORK"' EXIT HUP INT TERM
TOTAL=0
pass() { TOTAL=$((TOTAL + 1)); printf 'ok bootstrap-stability.%s - %s\n' "$TOTAL" "$1"; }
fail() { printf 'FAIL: %s\n' "$1" >&2; [ ! -s "$WORK/error" ] || cat "$WORK/error" >&2; exit 1; }
compile() {
    "$COMPILER" "$1" "$2.c" > "$WORK/log" 2> "$WORK/error" || fail "compile $1"
    if [ "${SANITIZE_GENERATED:-0}" = 1 ]; then
        "${CC:-cc}" -std=c11 -Wall -Wextra -Wpedantic -Werror -O1 -g \
            -fsanitize=address,undefined -fno-sanitize-recover=all \
            "$2.c" "$ROOT/selfhost/runtime.c" "$ROOT/selfhost/driver.c" -o "$2" > "$WORK/log" 2> "$WORK/error" || fail "C build $1"
    else
        "${CC:-cc}" -std=c11 -Wall -Wextra -Wpedantic -Werror "${KREMEN_TEST_OPT:--O3}" \
            "$2.c" "$ROOT/selfhost/runtime.c" "$ROOT/selfhost/driver.c" -o "$2" > "$WORK/log" 2> "$WORK/error" || fail "C build $1"
    fi
}
cat > "$WORK/order.kr" <<'KR'
extern func rt_arg(index i64) string
extern func rt_write(path string, value string) i64
extern func rt_number(value i64) string
extern func rt_cat(left string, right string) string
var counter i64 = 0
func tick() i64 { counter = counter + 1; return counter }
func mutate() i64 { counter = 9; return 2 }
func pair(a i64, b i64) i64 { return a * 10 + b }
func main() i64 {
    var ordered i64 = pair(tick(), tick())
    counter = 1
    var snapshot i64 = counter + mutate()
    counter = 0
    var sum i64 = 0
    for tick() < 4 {
        if counter == 2 { continue }
        sum = sum + counter
    }
    var loop_ticks i64 = counter
    counter = 0
    var first bool = false && tick() > 0
    var second bool = true || tick() > 0
    if first || !second { return 10 }
    var lazy_ticks i64 = counter
    if tick() == 1 {
        counter = counter + 1
    } else if tick() == 2 {
        return 11
    }
    if false {
        return 12
    } else if tick() == 3 {
        counter = counter + 1
    } else {
        return 13
    }
    var result string = rt_number(ordered)
    result = rt_cat(result, rt_cat("|", rt_number(snapshot)))
    result = rt_cat(result, rt_cat("|", rt_number(sum)))
    result = rt_cat(result, rt_cat("|", rt_number(loop_ticks)))
    result = rt_cat(result, rt_cat("|", rt_number(lazy_ticks)))
    result = rt_cat(result, rt_cat("|", rt_number(counter)))
    return rt_write(rt_arg(1), result)
}
KR
compile "$WORK/order.kr" "$WORK/order"
"$WORK/order" "$WORK/output" > "$WORK/log" 2> "$WORK/error" || fail 'ordered execution'
printf '12|3|4|4|0|4' > "$WORK/expected"
cmp -s "$WORK/expected" "$WORK/output" || fail 'ordered arguments/operands/lazy branches/loop continue'
pass 'left-to-right arguments and operand snapshots with lazy booleans/loop conditions'
cat > "$WORK/arithmetic.kr" <<'KR'
extern func rt_arg(index i64) string
extern func rt_equal(left string, right string) bool
var maximum i64 = 9223372036854775807
var minimum i64 = -9223372036854775808
func main() i64 {
    var mode string = rt_arg(1)
    if rt_equal(mode, "add") { return maximum + 1 }
    if rt_equal(mode, "sub") { return minimum - 1 }
    if rt_equal(mode, "mul") { return maximum * 2 }
    if rt_equal(mode, "div") { return minimum / -1 }
    if rt_equal(mode, "mod") { return minimum % -1 }
    if rt_equal(mode, "neg") { return -minimum }
    if rt_equal(mode, "zero-div") { return 1 / (maximum - maximum) }
    if rt_equal(mode, "zero-mod") { return 1 % (maximum - maximum) }
    var a i64 = maximum + 0
    var b i64 = minimum - 0
    var c i64 = maximum / 1
    var d i64 = minimum % 1
    if a != maximum || b != minimum || c != maximum || d != 0 { return 1 }
    return 0
}
KR
compile "$WORK/arithmetic.kr" "$WORK/arithmetic"
"$WORK/arithmetic" valid > "$WORK/log" 2> "$WORK/error" || fail 'valid arithmetic bounds'
pass 'valid i64 boundaries retain exact values'
for mode in add sub mul div mod neg zero-div zero-mod; do
    if "$WORK/arithmetic" "$mode" > "$WORK/log" 2> "$WORK/error"; then fail "missing arithmetic trap $mode"; fi
    grep -E 'arithmetic.kr:[0-9]+:[0-9]+: error:' "$WORK/error" > /dev/null || fail "missing source location $mode"
    case "$mode" in
        zero-div) expected='division by zero' ;;
        zero-mod) expected='remainder by zero' ;;
        *) expected='integer overflow' ;;
    esac
    grep -F "$expected" "$WORK/error" > /dev/null || fail "wrong arithmetic diagnostic $mode"
    if grep -E 'AddressSanitizer|UndefinedBehaviorSanitizer|runtime error:' "$WORK/error" > /dev/null; then fail "sanitizer failure $mode"; fi
    pass "checked arithmetic $mode"
done
awk 'BEGIN { printf "func main() i64 { return 1"; for(i=0;i<514;i++) printf "+1"; print " }" }' > "$WORK/long.kr"
printf 'previous output\n' > "$WORK/long.c"
if "$COMPILER" "$WORK/long.kr" "$WORK/long.c" > "$WORK/log" 2> "$WORK/error"; then fail 'long chain accepted'; fi
grep -F 'expression operation limit (512)' "$WORK/error" > /dev/null || fail 'long chain diagnostic'
grep -Fx 'previous output' "$WORK/long.c" > /dev/null || fail 'long chain replaced output'
pass 'long expression fails with bounded source diagnostic before publication'
cat > "$WORK/reserved.kr" <<'KR'
func main() i64 { var kr_bootstrap_value_1 i64 = 1; return kr_bootstrap_value_1 }
KR
if "$COMPILER" "$WORK/reserved.kr" "$WORK/reserved.c" > "$WORK/log" 2> "$WORK/error"; then fail 'generated temporary namespace accepted'; fi
grep -F 'reserved' "$WORK/error" > /dev/null || fail 'temporary namespace diagnostic'
pass 'generated temporary namespace is protected'
mkdir "$WORK/other"
cp "$WORK/order.kr" "$WORK/other/order.kr"
"$COMPILER" "$WORK/other/order.kr" "$WORK/relocated.c" > "$WORK/log" 2> "$WORK/error" || fail 'relocated generation'
cmp -s "$WORK/order.c" "$WORK/relocated.c" || fail 'generated C depends on checkout path'
pass 'stable source basenames preserve byte-identical C after relocation'
printf '\n%s bootstrap stability checks passed.\n' "$TOTAL"
