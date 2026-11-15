#!/bin/sh
# Match places without copying ignored storage; preserve patterns and checks.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
KREMEN=${1:-"$ROOT/build/kremen"}
case "$KREMEN" in /*) ;; *) KREMEN="$(pwd)/$KREMEN" ;; esac
WORK=$(mktemp -d "${TMPDIR:-/tmp}/kremen-optimization049.XXXXXX")
trap 'rm -rf "$WORK"' EXIT HUP INT TERM
TOTAL=0
fail() { printf 'FAIL: %s\n' "$1" >&2; [ ! -f "$WORK/error" ] || cat "$WORK/error" >&2; exit 1; }
pass() { TOTAL=$((TOTAL + 1)); printf 'ok optimization049.%s - %s\n' "$TOTAL" "$1"; }
exercise() {
    label=$1; expected_status=$2; diagnostic=$3; profiles=$4
    for profile in $profiles; do
        "$KREMEN" emit-c "$WORK/input.kr" --mode="$profile" -o "$WORK/program.c" > "$WORK/log" 2> "$WORK/error" || fail "$label: emit $profile"
        if [ -f "$WORK/ffi.c" ]; then cat "$WORK/ffi.c" >> "$WORK/program.c"; fi
        for opt in -O0 -O3; do
            if [ "${SANITIZE_GENERATED:-0}" = 1 ]; then
                "${CC:-cc}" -std=c11 -pthread -Wall -Wextra -Wpedantic -Werror "$opt" -g \
                    -fsanitize=address,undefined -fno-sanitize-recover=all "$WORK/program.c" -o "$WORK/program" 2> "$WORK/error" || fail "$label: instrument $profile $opt"
            else
                "${CC:-cc}" -std=c11 -pthread -Wall -Wextra -Wpedantic -Werror "$opt" "$WORK/program.c" -o "$WORK/program" 2> "$WORK/error" || fail "$label: compile $profile $opt"
            fi
            status=0
            (ulimit -s 8192; timeout 10 "$WORK/program") > "$WORK/output" 2> "$WORK/error" || status=$?
            [ "$status" -eq "$expected_status" ] || fail "$label: $profile $opt expected $expected_status, got $status"
            cmp "$WORK/expected" "$WORK/output" > /dev/null || fail "$label: changed effects or cleanup"
            if [ -n "$diagnostic" ]; then
                grep -F "$diagnostic" "$WORK/error" > /dev/null || fail "$label: lost runtime check"
            else
                [ ! -s "$WORK/error" ] || fail "$label: unexpected diagnostic"
            fi
            if grep -E 'AddressSanitizer|UndefinedBehaviorSanitizer' "$WORK/error" > /dev/null; then fail "$label: sanitizer failure"; fi
            pass "$label ($profile $opt)"
        done
    done
}

# Before 0.4.9 each match created another 800,008-byte stack snapshot.
awk 'BEGIN {
    print "type Choice enum { Empty; Data(values [100000]i64) }";
    print "func main() { var choice Choice";
    for(i=0;i<20;i++)print "match choice { Empty => {}; Data(_) => {} }";
    print "println(7)\n}";
}' > "$WORK/input.kr"
printf '7\n' > "$WORK/expected"
exercise 'repeated ignored enum payloads fit an 8 MiB stack' 0 '' work

awk 'BEGIN {
    print "func main() { var result ![100000]i64 = fail(7)";
    for(i=0;i<20;i++)print "match result { ok(_) => {}; fail(_) => {} }";
    print "println(7)\n}";
}' > "$WORK/input.kr"
exercise 'repeated ignored result payloads fit an 8 MiB stack' 0 '' work

awk 'BEGIN {
    print "type Choice enum { Data(number i64, values [100000]i64); Empty }";
    print "func main() { var choice Choice";
    for(i=0;i<20;i++)print "match choice { Data(number, _) => { assert(number == 0) }; Empty => {} }";
    print "println(7)\n}";
}' > "$WORK/input.kr"
exercise 'scalar patterns do not copy ignored large sibling payloads' 0 '' work

cat > "$WORK/input.kr" <<'KR'
type Choice enum { Data(values [2]i64, number i64); Empty }
func main() {
    var choice = Choice.Data([2]i64{4, 5}, 6)
    match choice {
        Data(values, number) => { choice = Choice.Empty(); println(values[0], values[1], number) }
        Empty => { println("wrong") }
    }
    match choice { Data(_, _) => { println("wrong") }; Empty => { println("empty") } }
    var result ![2]i64 = ok([2]i64{8, 9})
    match result {
        ok(values) => { result = fail(7); println(values[0], values[1]) }
        fail(code) => { println(code) }
    }
    match result { ok(_) => { println("wrong") }; fail(code) => { println(code) } }
}
KR
printf '4 5 6\nempty\n8 9\n7\n' > "$WORK/expected"
exercise 'bound payloads remain independent snapshots after subject reassignment' 0 '' 'work learn'

cat > "$WORK/input.kr" <<'KR'
type Choice enum { Data(number i64); Empty }
func source() [2]Choice { println("source"); return [2]Choice{Choice.Empty(), Choice.Data(9)} }
func index() i64 { println("index"); return 1 }
func produce() Choice { println("produce"); return Choice.Data(8) }
func main() {
    defer println("cleanup")
    match source()[index()] { Empty => {}; Data(number) => { println(number) } }
    match produce() { Empty => {}; Data(number) => { println(number) } }
}
KR
printf 'source\nindex\n9\nproduce\n8\ncleanup\n' > "$WORK/expected"
exercise 'rvalue and indexed producers run once in source order' 0 '' 'work learn'

cat > "$WORK/input.kr" <<'KR'
type Choice enum { Data(number i64); Empty }
type Holder struct { choice Choice }
func main() {
    var holder = Holder{choice: Choice.Data(7)}
    { var shared = &holder; match shared.choice { Data(number) => { println(number) }; Empty => {} } }
    unsafe { var pointer = raw(&holder); match (*pointer).choice { Data(number) => { println(number) }; Empty => {} } }
    var pair = (holder.choice, 0)
    match pair.0 { Data(number) => { println(number) }; Empty => {} }
    var result !Choice = ok(holder.choice)
    match result.value { Data(number) => { println(number) }; Empty => {} }
    match holder.choice { _ => { println("any") } }
    match result { _ => { println("any result") } }
}
KR
printf '7\n7\n7\n7\nany\nany result\n' > "$WORK/expected"
exercise 'reference raw pointer tuple result fields and wildcards remain valid' 0 '' work

cat > "$WORK/input.kr" <<'KR'
type Choice enum { Data(number i64); Empty }
func main() {
    defer println("cleanup")
    var choices = [1]Choice{Choice.Data(0)}
    var index = 1
    match choices[index] { Data(_) => {}; Empty => {} }
}
KR
printf 'cleanup\n' > "$WORK/expected"
exercise 'ignored matches retain bounds failure and cleanup' 1 'array index out of bounds' 'work learn'

cat > "$WORK/input.kr" <<'KR'
type Choice enum { Data(number i64); Empty }
func main() {
    defer println("cleanup")
    unsafe { var choices = alloc[Choice](1); free(choices); match choices[0] { Data(_) => {}; Empty => {} } }
}
KR
exercise 'ignored matches retain stale slice failure' 1 'slice use after free' 'work learn'

cat > "$WORK/input.kr" <<'KR'
type Choice enum { Data(number i64); Empty }
func main() {
    defer println("cleanup")
    var result !Choice = fail(7)
    match result.value { Data(_) => {}; Empty => {} }
}
KR
exercise 'nested result subject retains failure validation' 1 'access to failed result' 'work learn'

cat > "$WORK/input.kr" <<'KR'
type Choice enum { Data(number i64); Empty }
func main() {
    defer println("cleanup")
    var choices = [1]Choice{Choice.Data(0)}
    var index u64 = 18446744073709551615
    match choices[index] { Data(_) => {}; Empty => {} }
}
KR
exercise 'ignored matches retain unsigned index conversion checks' 1 'integer conversion out of range' 'work learn'

cat > "$WORK/input.kr" <<'KR'
type Choice enum { Data(number i64); Empty }
extern func corrupt(value &mut Choice)
func main() {
    defer println("cleanup")
    var choice = Choice.Data(7)
    unsafe { corrupt(&mut choice) }
    match choice { Data(_) => {}; Empty => {} }
}
KR
# A C ABI peer can violate the tag invariant; exhaustive matches must trap.
cat > "$WORK/ffi.c" <<'C'
void corrupt(kr_s_0_6_Choice *value) { value->tag = 99; }
C
exercise 'exhaustive matches still reject invalid foreign enum tags' 1 'invalid variant tag' 'work learn'

printf 'Optimization 0.4.9 checks passed: %s.\n' "$TOTAL"
