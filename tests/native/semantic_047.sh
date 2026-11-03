#!/bin/sh
# Discard aggregate places without copying them; retain checks and effects.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
KREMEN=${1:-"$ROOT/build/kremen"}
case "$KREMEN" in /*) ;; *) KREMEN="$(pwd)/$KREMEN" ;; esac
WORK=$(mktemp -d "${TMPDIR:-/tmp}/kremen-semantic047.XXXXXX")
trap 'rm -rf "$WORK"' EXIT HUP INT TERM
TOTAL=0
fail() { printf 'FAIL: %s\n' "$1" >&2; [ ! -f "$WORK/error" ] || cat "$WORK/error" >&2; exit 1; }
pass() { TOTAL=$((TOTAL + 1)); printf 'ok semantic047.%s - %s\n' "$TOTAL" "$1"; }
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

# 0.4.6 created twenty extra 800,000-byte copies in each of these programs.
awk 'BEGIN {
    print "func main() { var values [100000]i64";
    for(i=0;i<20;i++)print "_ = values";
    print "println(7)\n}";
}' > "$WORK/input.kr"
printf '7\n' > "$WORK/expected"
exercise 'repeated explicit array discards fit an 8 MiB stack' 0 '' work

awk 'BEGIN {
    print "func main() { var values ([100000]i64, i64)";
    for(i=0;i<20;i++)print "var (_, _) = values";
    print "println(7)\n}";
}' > "$WORK/input.kr"
exercise 'fully ignored tuple destructuring fits an 8 MiB stack' 0 '' work

cat > "$WORK/input.kr" <<'KR'
func source() [2][3]i64 {
    println("source")
    return [2][3]i64{[3]i64{1, 2, 3}, [3]i64{4, 5, 6}}
}
func index() i64 { println("index"); return 1 }
func scalar() i64 { println("scalar"); return 7 }
func main() { _ = source()[index()]; _ = source(); _ = scalar(); println("done") }
KR
printf 'source\nindex\nsource\nscalar\ndone\n' > "$WORK/expected"
exercise 'array and scalar producers retain effect and index order' 0 '' 'work learn'

cat > "$WORK/input.kr" <<'KR'
func source() ([2]i64, i64) {
    println("tuple")
    return ([2]i64{1, 2}, 9)
}
func main() {
    var (_, _) = source()
    var (_, kept) = source()
    println(kept)
}
KR
printf 'tuple\ntuple\n9\n' > "$WORK/expected"
exercise 'ignored tuples still call producers and partial patterns keep bindings' 0 '' 'work learn'

cat > "$WORK/input.kr" <<'KR'
type Holder struct { values [3]i64 }
type Choice enum { Data(values [2]i64); Empty }
func main() {
    var holder = Holder{values: [3]i64{1, 2, 3}}
    _ = holder
    _ = holder.values
    { var shared = &holder; _ = shared.values }
    unsafe { var pointer = raw(&holder); _ = (*pointer).values }
    var tuple = ([2]bool{true, false}, 9)
    _ = tuple
    _ = tuple.0
    var (_, _) = tuple
    var choice = Choice.Data([2]i64{4, 5})
    _ = choice
    println("done")
}
KR
printf 'done\n' > "$WORK/expected"
exercise 'aggregate fields tuples and address selections remain valid' 0 '' 'work learn'

cat > "$WORK/input.kr" <<'KR'
func main() {
    defer println("cleanup")
    var matrix = [2][2]i64{[2]i64{1, 2}, [2]i64{3, 4}}
    var index = 2
    _ = matrix[index]
}
KR
printf 'cleanup\n' > "$WORK/expected"
exercise 'discarded array selection retains bounds failure and cleanup' 1 'array index out of bounds' 'work learn'

cat > "$WORK/input.kr" <<'KR'
func main() {
    defer println("cleanup")
    unsafe {
        var values = alloc[([2]i64, i64)](1)
        free(values)
        var (_, _) = values[0]
    }
}
KR
exercise 'ignored tuple selection retains stale-slice failure' 1 'use after free' 'work learn'

cat > "$WORK/input.kr" <<'KR'
func failed() ![2]i64 { return fail(7) }
func main() {
    defer println("cleanup")
    var result = failed()
    _ = result.value
}
KR
exercise 'discarded result field retains failure validation' 1 'access to failed result' 'work learn'

cat > "$WORK/input.kr" <<'KR'
func main() {
    defer println("cleanup")
    var matrix = [2][2]i64{[2]i64{1, 2}, [2]i64{3, 4}}
    var index u64 = 18446744073709551615
    _ = matrix[index]
}
KR
exercise 'discarded array selection retains unsigned conversion checks' 1 'integer conversion out of range' 'work learn'

cat > "$WORK/input.kr" <<'KR'
func source() [2]i64 {
    var stop = true
    if stop { panic("source failed") }
    return [2]i64{1, 2}
}
func main() { defer println("cleanup"); _ = source() }
KR
exercise 'aggregate discard does not skip a failing producer' 1 'source failed' 'work learn'

for shape in direct tuple; do
    if [ "$shape" = direct ]; then
        printf 'func main() { var value !i64 = ok(7); _ = value }\n' > "$WORK/input.kr"
    else
        printf 'func main() { var value = (i64(1), readIntResult()); var (_, _) = value }\n' > "$WORK/input.kr"
    fi
    "$KREMEN" check "$WORK/input.kr" --mode=work > "$WORK/log" 2> "$WORK/error" || fail "$shape work compatibility"
    status=0
    "$KREMEN" check "$WORK/input.kr" --mode=learn > "$WORK/log" 2> "$WORK/error" || status=$?
    [ "$status" -eq 1 ] || fail "$shape learn status"
    grep -F 'L002' "$WORK/error" > /dev/null || fail "$shape learn result discard"
    pass "$shape result discard still requires handling in learn"
done
printf 'Semantic 0.4.7 checks passed: %s.\n' "$TOTAL"
