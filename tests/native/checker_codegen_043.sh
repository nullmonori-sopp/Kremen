#!/bin/sh
# Observable lowering, host ABI layout, canonical types, and worker boundaries.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
KREMEN=${1:-"$ROOT/build/kremen"}
case "$KREMEN" in /*) ;; *) KREMEN="$(pwd)/$KREMEN" ;; esac
WORK=$(mktemp -d "${TMPDIR:-/tmp}/kremen-checker-codegen043.XXXXXX")
trap 'rm -rf "$WORK"' EXIT HUP INT TERM
TOTAL=0
pass() { TOTAL=$((TOTAL + 1)); printf 'ok checker-codegen043.%s - %s\n' "$TOTAL" "$1"; }
fail() { printf 'FAIL: %s\n' "$1" >&2; [ ! -f "$WORK/error" ] || cat "$WORK/error" >&2; exit 1; }
reject() {
    label=$1; expected=$2
    cat > "$WORK/input.kr"
    status=0
    "$KREMEN" check "$WORK/input.kr" > "$WORK/log" 2> "$WORK/error" || status=$?
    [ "$status" -eq 1 ] || fail "$label: expected exit1, got $status"
    grep -F "$expected" "$WORK/error" > /dev/null || fail "$label: missing $expected"
    if grep -E 'AddressSanitizer|UndefinedBehaviorSanitizer' "$WORK/error" > /dev/null; then fail "$label: sanitizer failure"; fi
    pass "$label"
}
compile() {
    "$KREMEN" emit-c "$WORK/input.kr" -o "$WORK/program.c" > "$WORK/log" 2> "$WORK/error" || fail 'emit fixture'
    if [ "${SANITIZE_GENERATED:-0}" = 1 ]; then
        "${CC:-cc}" -std=c11 -pthread -Wall -Wextra -Wpedantic -Werror -O1 -g \
            -fsanitize=address,undefined -fno-sanitize-recover=all "$WORK/program.c" -o "$WORK/program" 2> "$WORK/error" || fail 'instrument fixture'
    else
        "${CC:-cc}" -std=c11 -pthread -Wall -Wextra -Wpedantic -Werror -O3 "$WORK/program.c" -o "$WORK/program" 2> "$WORK/error" || fail 'compile fixture'
    fi
}

cat > "$WORK/input.kr" <<'KR'
func source() [2][3]i64 {
    println("evaluated")
    return [2][3]i64{[3]i64{1, 2, 3}, [3]i64{4, 5, 6}}
}
func index() i64 { println("indexed"); return 1 }
func main() { println(-len(source()[index()])) }
KR
compile
"$WORK/program" > "$WORK/output" 2> "$WORK/error" || fail 'execute side effects'
printf 'evaluated\nindexed\n-3\n' > "$WORK/expected"
cmp "$WORK/expected" "$WORK/output" || fail 'negated fixed length dropped or reordered effects'
pass 'negated fixed-array length evaluates source and index exactly once in order'

cat > "$WORK/input.kr" <<'KR'
func main() {
    defer println("cleanup")
    var matrix = [2][2]i64{[2]i64{1, 2}, [2]i64{3, 4}}
    var index = readInt()
    println(-len(matrix[index]))
}
KR
compile
printf '2\n' > "$WORK/input"
status=0
"$WORK/program" < "$WORK/input" > "$WORK/output" 2> "$WORK/error" || status=$?
[ "$status" -eq 1 ] || fail 'negated fixed length skipped bounds check'
grep -F 'array index out of bounds' "$WORK/error" > /dev/null || fail 'wrong bounds diagnostic'
if grep -E 'AddressSanitizer|UndefinedBehaviorSanitizer' "$WORK/error" > /dev/null; then fail 'unsafe generated bounds arithmetic'; fi
printf 'cleanup\n' > "$WORK/expected"
cmp "$WORK/expected" "$WORK/output" || fail 'bounds failure skipped cleanup'
pass 'negated fixed length preserves dynamic bounds checks and deferred cleanup'

cat > "$WORK/input.kr" <<'KR'
func identity(value i64) i64 { return value }
func main() {
    println(-9223372036854775808)
    println(-identity(3), -(1 + 2), -(-3))
}
KR
compile
"$WORK/program" > "$WORK/output" 2> "$WORK/error" || fail 'execute negations'
printf '%s\n' '-9223372036854775808' '-3 -3 3' > "$WORK/expected"
cmp "$WORK/expected" "$WORK/output" || fail 'integer negation changed'
pass 'minimum integer literal and checked compound negations retain their values'

cat > "$WORK/input.kr" <<'KR'
type Compact struct { values [60000]!i8 }
type Views struct { values [40000][]i64 }
func main() { println("layout") }
KR
"$KREMEN" emit-c "$WORK/input.kr" -o "$WORK/layout.c" > "$WORK/log" 2> "$WORK/error" || fail 'small result and slice arrays rejected'
cat >> "$WORK/layout.c" <<'C'
_Static_assert(sizeof(kr_s_0_7_Compact) == 60000 * sizeof(struct { bool ok; int8_t value; int64_t code; }), "result ABI layout");
_Static_assert(sizeof(kr_s_0_5_Views) == 40000 * sizeof(struct { void *owner; size_t offset, length; }), "slice ABI layout");
C
"${CC:-cc}" -std=c11 -pthread -Wall -Wextra -Wpedantic -Werror "$WORK/layout.c" -o "$WORK/program" 2> "$WORK/error" || fail 'layout differs from actual generated C'
pass 'result and slice layout matches host C instead of pessimistic fixed sizes'

reject 'true oversized result array remains rejected' '1 MiB' <<'KR'
type Oversized struct { values [100000]!i8 }
func main() {}
KR
reject 'true oversized slice array remains rejected' '1 MiB' <<'KR'
type Oversized struct { values [100000][]i64 }
func main() {}
KR
reject 'memoized layout detects recursive value cycle' 'recursive value type' <<'KR'
type Recursive struct { next Recursive }
func main() {}
KR
cat > "$WORK/input.kr" <<'KR'
type Tree struct { children []Tree }
func main() { var value Tree; println(len(value.children)) }
KR
compile
"$WORK/program" > "$WORK/output" 2> "$WORK/error" || fail 'recursive slice layout'
printf '0\n' > "$WORK/expected"
cmp "$WORK/expected" "$WORK/output" || fail 'recursive slice changed value'
pass 'recursive slice type has finite layout and terminates backend type scanning'

awk 'BEGIN {
    for(i=1;i<=96;i++)printf "func f%d(value i64) [%d]i64 { var values [%d]i64; values[0] = value; return values }\n",i,i,i;
    print "func main() { var total = 0";
    for(i=1;i<=96;i++)printf "total += f%d(%d)[0]\n",i,i;
    print "println(total) }"
}' > "$WORK/input.kr"
compile
"$WORK/program" > "$WORK/output" 2> "$WORK/error" || fail 'execute many canonical types'
printf '4656\n' > "$WORK/expected"
cmp "$WORK/expected" "$WORK/output" || fail 'type table growth changed returned array values'
pass 'backend type hash table grows across 96 distinct array types'

cat > "$WORK/input.kr" <<'KR'
type Wrapper struct { pair (i64, bool) }
func pair(number i64) (i64, bool) { return (number, true) }
func wrapped(number i64) Wrapper { return Wrapper{pair: pair(number)} }
func check(value (i64, bool)) !i64 { if value.1 { return ok(value.0) }; return fail(9) }
func main() {
    var first = wrapped(3)
    var second = pair(4)
    println(check(first.pair) catch -1, check(second) catch -1)
}
KR
compile
"$WORK/program" > "$WORK/output" 2> "$WORK/error" || fail 'execute equivalent types'
printf '3 4\n' > "$WORK/expected"
cmp "$WORK/expected" "$WORK/output" || fail 'equivalent structural tuple types were not canonicalized'
pass 'structurally equal tuples/results remain compatible across calls and fields'

reject 'unknown local gets a unique spelling suggestion' "Did you mean 'counter'?" <<'KR'
func main() { var counter = 1; println(coutner); println(counter) }
KR
grep -F 'suggested declaration is here' "$WORK/error" > /dev/null || fail 'suggestion missing declaration location'
reject 'unknown user function gets a spelling suggestion' "Did you mean 'calculate'?" <<'KR'
func calculate(number i64) i64 { return number + 1 }
func main() { println(calcluate(2)) }
KR
reject 'unknown builtin gets a spelling suggestion' "Did you mean 'println'?" <<'KR'
func main() { pritnln(2) }
KR
reject 'unknown type gets a spelling suggestion' "Did you mean 'Counter'?" <<'KR'
type Counter struct { number i64 }
func main() { var value Counetr; println(value.number) }
KR
reject 'primitive type misspelling gets a precise suggestion' "Did you mean 'bool'?" <<'KR'
func main() { var value bol; println(value) }
KR
reject 'unknown struct field gets a spelling suggestion' "Did you mean 'number'?" <<'KR'
type Counter struct { number i64 }
func main() { var value = Counter{number: 1}; println(value.nubmer) }
KR
reject 'unknown method gets a receiver-specific suggestion' "Did you mean 'current'?" <<'KR'
type Counter struct { number i64 }
func (value Counter) current() i64 { return value.number }
func main() { var value = Counter{number: 1}; println(value.curernt()) }
KR
reject 'unknown enum variant gets a spelling suggestion' "Did you mean 'Missing'?" <<'KR'
type Reading enum { Missing; Number(value i64) }
func main() { var value = Reading.Missnig(); match value { _ => {} } }
KR
reject 'ambiguous spelling does not guess a declaration' 'unknown variable' <<'KR'
func main() { var cat = 1; var cut = 2; println(cot); println(cat, cut) }
KR
if grep -F 'Did you mean' "$WORK/error" > /dev/null; then fail 'ambiguous suggestion'; fi
reject 'expired local names are excluded from suggestions' 'unknown variable' <<'KR'
func main() { { var hidden = 1; println(hidden) }; println(hiddden) }
KR
if grep -F 'Did you mean' "$WORK/error" > /dev/null; then fail 'expired binding leaked into suggestions'; fi

reject 'worker requires one signed i64 parameter' 'E150' <<'KR'
func worker(value u64) i64 { return i64(value) }
func main() { var task = threadSpawn(worker, 1); println(task.ok) }
KR
reject 'worker must return i64' 'E150' <<'KR'
func worker(value i64) !i64 { return ok(value) }
func main() { var task = threadSpawn(worker, 1); println(task.ok) }
KR
reject 'worker cannot have two parameters' 'E150' <<'KR'
func worker(left i64, right i64) i64 { return left + right }
func main() { var task = threadSpawn(worker, 1); println(task.ok) }
KR
reject 'foreign function cannot serve as a Kremen worker' 'E150' <<'KR'
extern func externalWorker(value i64) i64
func main() { var task = threadSpawn(externalWorker, 1); println(task.ok) }
KR
reject 'worker is a named function rather than arbitrary expression' 'E150' <<'KR'
func main() { var task = threadSpawn(7, 1); println(task.ok) }
KR
reject 'worker argument has strict integer type' 'type mismatch' <<'KR'
func worker(value i64) i64 { return value }
func main() { var argument i32 = 1; var task = threadSpawn(worker, argument); println(task.ok) }
KR
reject 'thread handles cannot escape a function result' 'E150' <<'KR'
func escaping(value thread) thread { return value }
func main() {}
KR
reject 'thread result handles cannot escape a function result' 'E150' <<'KR'
func worker(value i64) i64 { return value }
func escaping() !thread { return threadSpawn(worker, 1) }
func main() {}
KR
reject 'thread handles cannot inhabit arrays' 'E150' <<'KR'
func main() { var values [2]thread; println(len(values)) }
KR
reject 'thread handles cannot inhabit allocated slices' 'E150' <<'KR'
func main() { unsafe { var values = alloc[thread](2); free(values) } }
KR
reject 'thread handles cannot inhabit structs' 'E150' <<'KR'
type Invalid struct { task thread }
func main() {}
KR
reject 'thread handles cannot inhabit enum payloads' 'E150' <<'KR'
type Invalid enum { Task(value thread) }
func main() {}
KR
reject 'thread handles cannot inhabit tuples' 'E150' <<'KR'
func invalid(task thread) { var pair = (task, 1); println(pair.1) }
func main() {}
KR
reject 'thread handles cannot be borrowed' 'E150' <<'KR'
func invalid(task thread) { var reference = &task; _ = *reference }
func main() {}
KR
reject 'thread handles cannot inhabit nested result containers' 'E150' <<'KR'
func main() { var nested !!thread = fail(1); println(nested.ok) }
KR

cat > "$WORK/input.kr" <<'KR'
func worker(value i64) i64 { return value * 2 }
func execute(value i64) !i64 {
    var task = threadSpawn(worker, value)?
    defer threadClose(task)
    return threadJoin(task)
}
func main() { println(execute(21) catch -1) }
KR
compile
"$WORK/program" > "$WORK/output" 2> "$WORK/error" || fail 'worker result propagation'
printf '42\n' > "$WORK/expected"
cmp "$WORK/expected" "$WORK/output" || fail 'worker typed result changed'
pass 'spawn error propagation and same-creator deferred join work in result functions'

mkdir -p "$WORK/workers"
printf 'func double(value i64) i64 { return value * 2 }\n' > "$WORK/workers/math.kr"
cat > "$WORK/input.kr" <<'KR'
import "workers/math"
func main() {
    var started = threadSpawn(math.double, 21)
    match started {
        ok(task) => { defer threadClose(task); println(threadJoin(task) catch -1) }
        fail(code) => { println(code) }
    }
}
KR
compile
"$WORK/program" > "$WORK/output" 2> "$WORK/error" || fail 'imported worker execution'
printf '42\n' > "$WORK/expected"
cmp "$WORK/expected" "$WORK/output" || fail 'imported worker had wrong result'
pass 'named workers in relative imports lower to their typed function pointer'

reject 'known import with misspelled function reports its member instead of a local variable' "unknown function 'dobule' in imported module 'math'" <<'KR'
import "workers/math"
func main() { println(math.dobule(21)) }
KR
grep -F "Did you mean 'double'?" "$WORK/error" > /dev/null || fail 'imported function suggestion missing'

printf 'Checker/codegen 0.4.3 passed: %s checks.\n' "$TOTAL"
