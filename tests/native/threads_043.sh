#!/bin/sh
# End-to-end pthread code generation and the first concurrency safety boundary.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
KREMEN=${1:-"$ROOT/build/kremen"}
case "$KREMEN" in /*) ;; *) KREMEN="$(pwd)/$KREMEN" ;; esac
WORK=$(mktemp -d "${TMPDIR:-/tmp}/kremen-threads-language.XXXXXX")
trap 'rm -rf "$WORK"' EXIT HUP INT TERM
TOTAL=0
pass() { TOTAL=$((TOTAL + 1)); printf 'ok threads-language.%s - %s\n' "$TOTAL" "$1"; }
fail() { printf 'FAIL: %s\n' "$1" >&2; cat "$WORK/error" >&2; exit 1; }
build() {
    if [ "${SANITIZE_GENERATED:-0}" = 1 ]; then
        "$KREMEN" emit-c "$WORK/input.kr" -o "$WORK/input.c" > "$WORK/log" 2> "$WORK/error" || fail 'emit thread program'
        "${CC:-cc}" -std=c11 -pthread -Wall -Wextra -Wpedantic -Werror -O1 -g \
            -fsanitize=address,undefined -fno-sanitize-recover=all "$WORK/input.c" -o "$WORK/program" 2> "$WORK/error" || fail 'compile thread program with sanitizers'
    else
        "$KREMEN" build "$WORK/input.kr" -O3 -o "$WORK/program" > "$WORK/log" 2> "$WORK/error" || fail 'build thread program'
    fi
}
run_expected() {
    name=$1; expected=$2
    build
    timeout 15 "$WORK/program" > "$WORK/output" 2> "$WORK/error" || fail "$name"
    printf '%s\n' "$expected" > "$WORK/expected"
    cmp -s "$WORK/expected" "$WORK/output" || fail "$name output"
    pass "$name"
}
reject() {
    name=$1; diagnostic=$2
    cat > "$WORK/input.kr"
    status=0
    "$KREMEN" check "$WORK/input.kr" > "$WORK/output" 2> "$WORK/error" || status=$?
    [ "$status" -eq 1 ] || fail "$name expected diagnostic status1, got $status"
    grep -F "$diagnostic" "$WORK/error" > /dev/null || fail "$name wrong diagnostic"
    pass "$name"
}
cp "$ROOT/examples/threads.kr" "$WORK/input.kr"
run_expected 'spawn/join results and deferred idempotent close' 'sum of squares: 100'

cat > "$WORK/input.kr" <<'KR'
func failWorker(value i64) i64 {
    defer println("released", value)
    panic("worker stopped")
    return value
}
func main() {
    var handle = threadSpawn(failWorker, 7)
    assert(handle.ok)
    var answer = threadJoin(handle.value)
    println(answer.ok, answer.code > 0)
    threadClose(handle.value)
}
KR
# panic is a runtime stop; use a conditional so all-path flow checking remains valid.
sed 's/panic("worker stopped")/if value == 7 { panic("worker stopped") }/' "$WORK/input.kr" > "$WORK/rewritten.kr"
mv "$WORK/rewritten.kr" "$WORK/input.kr"
run_expected 'worker panic runs its defer and becomes a handled result' 'released 7
false true'
grep -F 'input.kr:' "$WORK/error" > /dev/null || fail 'worker panic missing source location'
grep -F 'worker started with argument 7; threadJoin reports ECANCELED' "$WORK/error" > /dev/null || fail 'worker panic missing job context'

cat > "$WORK/input.kr" <<'KR'
func square(value i64) i64 { return value * value }
func nested(value i64) i64 {
    var child = threadSpawn(square, value)
    if !child.ok { return -1 }
    defer threadClose(child.value)
    return (threadJoin(child.value) catch -2) + 1
}
func main() {
    var child = threadSpawn(nested, 8)
    assert(child.ok)
    println(threadJoin(child.value) catch -3)
}
KR
run_expected 'workers can spawn and join their own children' '65'

mkdir -p "$WORK/jobs"
printf 'func square(value i64) i64 { return value * value }\n' > "$WORK/jobs/work.kr"
cat > "$WORK/input.kr" <<'KR'
import "jobs/work"
func main() {
    var job = threadSpawn(work.square, 9)
    assert(job.ok)
    println(threadJoin(job.value) catch -1)
}
KR
run_expected 'import-qualified worker function survives C emission' '81'

cat > "$WORK/input.kr" <<'KR'
func argument() i64 { println("argument"); return 5 }
func worker(value i64) i64 { println("worker", value); return value }
func main() {
    var job = threadSpawn(worker, argument())
    assert(job.ok)
    println("result", threadJoin(job.value) catch -1)
}
KR
run_expected 'spawn argument executes once before the worker starts' 'argument
worker 5
result 5'

cat > "$WORK/input.kr" <<'KR'
func worker(value i64) i64 {
    for index := 0; index < 50; index++ { println("worker", value, "iteration", index) }
    return value
}
func main() {
    var first = threadSpawn(worker, 3)
    var second = threadSpawn(worker, 7)
    assert(first.ok && second.ok)
    _ = threadJoin(first.value)
    _ = threadJoin(second.value)
}
KR
build
timeout 15 "$WORK/program" > "$WORK/output" 2> "$WORK/error" || fail 'concurrent output'
[ "$(wc -l < "$WORK/output")" -eq 100 ] || fail 'lost output lines'
if grep -Ev '^worker [37] iteration [0-9]+$' "$WORK/output"; then fail 'interleaved output call'; fi
LC_ALL=C sort "$WORK/output" | uniq > "$WORK/unique"
[ "$(wc -l < "$WORK/unique")" -eq 100 ] || fail 'duplicated or lost worker values'
pass 'generated println groups each complete output call'

cat > "$WORK/input.kr" <<'KR'
func worker(value i64) i64 { println("finished", value); return value }
func main() { _ = threadSpawn(worker, 9) }
KR
run_expected 'discarded spawn result still joins at process exit' 'finished 9'

cat > "$WORK/input.kr" <<'KR'
func worker(value i64) i64 {
    defer println("released", value)
    if value == 9 { panic("unobserved worker failure") }
    return value
}
func main() { _ = threadSpawn(worker, 9) }
KR
build
status=0
timeout 15 "$WORK/program" > "$WORK/output" 2> "$WORK/error" || status=$?
[ "$status" -eq 1 ] || fail 'unobserved worker failure expected status1'
grep -Fx 'released 9' "$WORK/output" > /dev/null || fail 'unobserved worker defer'
grep -F 'unobserved worker failure' "$WORK/error" > /dev/null || fail 'unobserved worker diagnostic'
pass 'unobserved worker failure cannot silently produce successful exit'

reject 'worker must return i64' E150 <<'KR'
func worker(value i64) bool { return value > 0 }
func main() { _ = threadSpawn(worker, 9) }
KR
reject 'worker input must be i64' E150 <<'KR'
func worker(value i32) i64 { return i64(value) }
func main() { _ = threadSpawn(worker, 9) }
KR
reject 'worker has exactly one parameter' E150 <<'KR'
func worker(left i64, right i64) i64 { return left + right }
func main() { _ = threadSpawn(worker, 9) }
KR
reject 'external C function is not a checked worker' E150 <<'KR'
extern func worker(value i64) i64
func main() { _ = threadSpawn(worker, 9) }
KR
reject 'an ordinary value cannot act as a worker function' E150 <<'KR'
func main() { var worker = 9; _ = threadSpawn(worker, 9) }
KR
reject 'worker argument has strict type checking' E110 <<'KR'
func worker(value i64) i64 { return value }
func main() { _ = threadSpawn(worker, true) }
KR
reject 'worker argument cannot transfer a reference' E110 <<'KR'
func worker(value i64) i64 { return value }
func main() { var value = 9; _ = threadSpawn(worker, &value) }
KR
reject 'thread handles cannot be struct fields' E150 <<'KR'
type Holder struct { task thread }
func main() { println("unused type") }
KR
reject 'thread handles cannot be tuple values' E150 <<'KR'
func worker(value i64) i64 { return value }
func main() {
    var task = threadSpawn(worker, 9)
    assert(task.ok)
    var pair = (task.value, task.value)
    _ = pair
}
KR
reject 'user functions cannot return worker handles' E150 <<'KR'
func worker(value i64) i64 { return value }
func start() !thread { return threadSpawn(worker, 9) }
func main() { _ = start() }
KR
reject 'references cannot point to thread handles' E150 <<'KR'
func worker(value i64) i64 { return value }
func main() {
    var task = threadSpawn(worker, 9)
    assert(task.ok)
    var handle = task.value
    var reference = &handle
    _ = reference
}
KR
printf 'Thread language passed: %s checks.\n' "$TOTAL"
