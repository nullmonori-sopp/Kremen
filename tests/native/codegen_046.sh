#!/bin/sh
# Typed staged operands and fixed-array length evaluation without value copies.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
KREMEN=${1:-"$ROOT/build/kremen"}
case "$KREMEN" in /*) ;; *) KREMEN="$(pwd)/$KREMEN" ;; esac
WORK=$(mktemp -d "${TMPDIR:-/tmp}/kremen-codegen046.XXXXXX")
trap 'rm -rf "$WORK"' EXIT HUP INT TERM
TOTAL=0
fail() { printf 'FAIL: %s\n' "$1" >&2; [ ! -f "$WORK/error" ] || cat "$WORK/error" >&2; exit 1; }
pass() { TOTAL=$((TOTAL + 1)); printf 'ok codegen046.%s - %s\n' "$TOTAL" "$1"; }
exercise() {
    label=$1; expected_status=$2; diagnostic=$3; profiles=$4
    for profile in $profiles; do
        "$KREMEN" emit-c "$WORK/input.kr" --mode="$profile" -o "$WORK/program.c" > "$WORK/log" 2> "$WORK/error" || fail "$label: emit $profile"
        for opt in -O0 -O3; do
            if [ "${SANITIZE_GENERATED:-0}" = 1 ]; then
                "${CC:-cc}" -std=c11 -pthread -Wall -Wextra -Wpedantic -Werror "$opt" -g \
                    -fsanitize=address,undefined -fno-sanitize-recover=all "$WORK/program.c" -o "$WORK/program" 2> "$WORK/error" || fail "$label: instrument $profile $opt"
            else
                "${CC:-cc}" -std=c11 -pthread -Wall -Wextra -Wpedantic -Werror "$opt" "$WORK/program.c" -o "$WORK/program" 2> "$WORK/error" || fail "$label: compile $profile $opt"
            fi
            status=0
            (ulimit -s 8192; "$WORK/program") > "$WORK/output" 2> "$WORK/error" || status=$?
            [ "$status" -eq "$expected_status" ] || fail "$label: $profile $opt expected $expected_status, got $status"
            cmp "$WORK/expected" "$WORK/output" > /dev/null || fail "$label: changed source effects or cleanup"
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

# 0.4.5 created twenty extra 800,000-byte C temporaries here. At -O0,
# its >16 MiB frame crashed before main's first statement on an 8 MiB stack.
awk 'BEGIN {
    print "func main() { var values [100000]i64";
    for(i=0;i<20;i++)print "println(len(values))";
    print "}";
}' > "$WORK/input.kr"
awk 'BEGIN {for(i=0;i<20;i++)print "100000"}' > "$WORK/expected"
exercise 'repeated large-array lengths fit an 8 MiB stack' 0 '' work

cat > "$WORK/input.kr" <<'KR'
func source() [2][3]i64 {
    println("source")
    return [2][3]i64{[3]i64{1, 2, 3}, [3]i64{4, 5, 6}}
}
func index() i64 { println("index"); return 1 }
func main() { println(-len(source()[index()])) }
KR
printf 'source\nindex\n-3\n' > "$WORK/expected"
exercise 'discarded fixed-array values retain calls and indexing order' 0 '' 'work learn'

cat > "$WORK/input.kr" <<'KR'
type Holder struct { values [3]i64 }
func main() {
    var holder = Holder{values: [3]i64{1, 2, 3}}
    println(len(holder.values))
    { var shared = &holder; println(len(shared.values)) }
    unsafe { var pointer = raw(&holder); println(len((*pointer).values)) }
    var tuple = ([2]bool{true, false}, 9)
    println(len(tuple.0), len(tuple[0]))
}
KR
printf '3\n3\n3\n2 2\n' > "$WORK/expected"
exercise 'typed fields tuples and address paths retain fixed lengths' 0 '' 'work learn'

cat > "$WORK/input.kr" <<'KR'
type Box struct { value i64 }
func box() Box { println("receiver"); return Box{value: 10} }
func argument(label string, value i64) i64 { println(label); return value }
func (value Box) sum(left i64, right i64) i64 { return value.value + left + right }
func (value Box) show(extra i64) { println(value.value + extra) }
func main() {
    println(box().sum(argument("left", 1), argument("right", 2)))
    println(argument("first", 3), true, "typed")
    defer box().show(argument("captured", 4))
    println("body")
}
KR
printf 'receiver\nleft\nright\n13\nfirst\n3 true typed\nreceiver\ncaptured\nbody\n14\n' > "$WORK/expected"
exercise 'typed call operands preserve receivers arguments and deferred capture' 0 '' 'work learn'

cat > "$WORK/input.kr" <<'KR'
func main() {
    defer println("cleanup")
    var matrix = [2][2]i64{[2]i64{1, 2}, [2]i64{3, 4}}
    var index = 2
    println(len(matrix[index]))
}
KR
printf 'cleanup\n' > "$WORK/expected"
exercise 'discarded array selection retains bounds failure and cleanup' 1 'array index out of bounds' 'work learn'

cat > "$WORK/input.kr" <<'KR'
func main() {
    defer println("cleanup")
    unsafe {
        var values = alloc[[2]i64](1)
        free(values)
        println(len(values[0]))
    }
}
KR
exercise 'discarded array selection retains stale-slice failure' 1 'use after free' 'work learn'

cat > "$WORK/input.kr" <<'KR'
func failed() ![2]i64 { return fail(7) }
func main() {
    defer println("cleanup")
    var result = failed()
    println(len(result.value))
}
KR
exercise 'discarded result field retains failure validation' 1 'access to failed result' 'work learn'

cat > "$WORK/input.kr" <<'KR'
func main() {
    defer println("cleanup")
    var matrix = [2][2]i64{[2]i64{1, 2}, [2]i64{3, 4}}
    var index u64 = 18446744073709551615
    println(len(matrix[index]))
}
KR
exercise 'discarded array selection retains checked unsigned conversion' 1 'integer conversion out of range' 'work learn'

cat > "$WORK/input.kr" <<'KR'
func source() [2]i64 {
    var stop = true
    if stop { panic("source failed") }
    return [2]i64{1, 2}
}
func main() { defer println("cleanup"); println(len(source())) }
KR
exercise 'fixed-array length does not skip a failing producer' 1 'source failed' 'work learn'

printf 'Codegen 0.4.6 checks passed: %s.\n' "$TOTAL"
