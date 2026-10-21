#!/bin/sh
# Release regressions: resources, ADTs, tuples, diagnostics and formatter.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
KREMEN=${1:-"$ROOT/build/kremen"}
case "$KREMEN" in /*) ;; *) KREMEN="$(pwd)/$KREMEN" ;; esac
WORK=$(mktemp -d "${TMPDIR:-/tmp}/kremen-v3-tests.XXXXXX")
trap 'rm -rf "$WORK"' EXIT HUP INT TERM
TOTAL=0
pass() { TOTAL=$((TOTAL + 1)); printf 'ok v3.%s - %s\n' "$TOTAL" "$1"; }
fail() { printf 'FAIL: %s\n' "$1" >&2; cat "$WORK/error" >&2; exit 1; }
build() {
    source=$1
    output=$2
    if [ "${SANITIZE_GENERATED:-0}" = 1 ]; then
        "$KREMEN" emit-c "$source" -o "$output.c" > "$WORK/log" 2> "$WORK/error" || fail "emit $source"
        "${CC:-cc}" -std=c11 -Wall -Wextra -Wpedantic -Werror -O1 -g \
            -fsanitize=address,undefined -fno-sanitize-recover=all "$output.c" -o "$output" > "$WORK/log" 2> "$WORK/error" || fail "sanitize $source"
    else
        "$KREMEN" build "$source" -o "$output" > "$WORK/log" 2> "$WORK/error" || fail "build $source"
    fi
}
expect() {
    printf '%s\n' "$2" > "$WORK/expected"
    cmp -s "$WORK/expected" "$WORK/actual" || { diff -u "$WORK/expected" "$WORK/actual" >&2 || :; fail "$1"; }
    pass "$1"
}
reject() {
    name=$1; diagnostic=$2
    cat > "$WORK/invalid.kr"
    if "$KREMEN" check "$WORK/invalid.kr" > "$WORK/log" 2> "$WORK/error"; then fail "$name accepted"; fi
    grep -F "$diagnostic" "$WORK/error" > /dev/null || fail "$name diagnostic"
    grep -F 'invalid.kr:' "$WORK/error" > /dev/null || fail "$name location"
    pass "$name"
}
build "$ROOT/tests/native/v3_values.kr" "$WORK/values"
"$WORK/values" > "$WORK/actual" 2> "$WORK/error" || fail values
expect 'defer captures/LIFO/loop/?, tuples, match, catch, arena reset' 'body 4
captured 3
loop 0
loop 1
9 seven
0 5 9
0
10
6
leave -1
error 9
3 0 42
11 true
outer 2
outer 1'
build "$ROOT/tests/native/v3_fs.kr" "$WORK/fs"
mkdir "$WORK/files"
(cd "$WORK/files" && "$WORK/fs") > "$WORK/actual" 2> "$WORK/error" || fail filesystem
expect 'filesystem, binary strings, deterministic listing, stale handles, NUL paths' 'hello!
6 true
1 b
3
3
false true
false true
false true
false'
build "$ROOT/tests/native/v3_traps.kr" "$WORK/traps"
for entry in '0|boom' '1|assertion failed' '2|slice use after free' '3|slice use after free' \
    '4|arena' '5|arena use after free' '6|slice use after free' '7|arena double free' '8|negative arena allocation length'; do
    mode=${entry%%|*}; diagnostic=${entry#*|}
    printf '%s\n' "$mode" > "$WORK/input"
    if "$WORK/traps" < "$WORK/input" > "$WORK/actual" 2> "$WORK/error"; then fail "trap $mode missing"; fi
    grep -F "$diagnostic" "$WORK/error" > /dev/null || fail "trap $mode diagnostic"
    grep -F 'outer' "$WORK/actual" > /dev/null || fail "trap $mode skipped defer"
    if [ "$mode" = 0 ]; then expect 'panic cleans caller and callee in LIFO order' 'inner
outer'; else pass "arena/assert safety trap $mode and defer"; fi
    if grep -E 'AddressSanitizer|UndefinedBehaviorSanitizer|runtime error:.*(misaligned|overflow)' "$WORK/error" > /dev/null; then fail "sanitizer failure $mode"; fi
done
cat > "$WORK/edge.kr" <<'KR'
type Flag enum { No; Yes }
func unit() () { return () }
func bump(value &mut i64) { *value += 1 }
func checked(n i64) !i64 { if n == 0 { return fail(3) }; return ok(n) }
func fallback() i64 { println("fallback"); return 8 }
func main() {
    var (_, _) = (unit(), 1)
    var (single) = (4,)
    println(single)
    var value = 9
    { defer bump(&mut value) }
    println(value)
    println(checked(2) catch fallback())
    println(checked(0) catch fallback())
    var yes = Flag.Yes()
    match yes { _ => { println("wildcard") } }
    for {
        match yes { No => { continue }; Yes => { break } }
    }
    println("done")
}
KR
build "$WORK/edge.kr" "$WORK/edge"
"$WORK/edge" > "$WORK/actual" 2> "$WORK/error" || fail edge
expect 'unit/single tuples, wildcard, mutable destructor, lazy catch, match loop exits' '4
10
2
fallback
8
wildcard
done'
for fixture in v3_values v3_fs v3_traps; do
    "$KREMEN" fmt "$ROOT/tests/native/$fixture.kr" > "$WORK/$fixture.kr" 2> "$WORK/error" || fail "format $fixture"
    "$KREMEN" fmt "$WORK/$fixture.kr" > "$WORK/again.kr" 2> "$WORK/error" || fail "reformat $fixture"
    cmp -s "$WORK/$fixture.kr" "$WORK/again.kr" || fail "format $fixture not idempotent"
    "$KREMEN" check "$WORK/$fixture.kr" > "$WORK/log" 2> "$WORK/error" || fail "formatted $fixture semantics"
    pass "formatter preserves $fixture syntax and types"
done
build "$WORK/v3_values.kr" "$WORK/formatted"
"$WORK/formatted" > "$WORK/formatted.output" 2> "$WORK/error" || fail 'formatted execution'
"$WORK/values" > "$WORK/actual" 2> "$WORK/error" || fail values
cmp -s "$WORK/actual" "$WORK/formatted.output" || fail 'formatter changed execution'
pass 'formatter preserves v3 behavior'
reject 'non-exhaustive match' 'non-exhaustive' <<'KR'
type E enum { A; B }
func main() { var e = E.A(); match e { A => { println(1) } } }
KR
reject 'duplicate pattern' 'duplicate' <<'KR'
type E enum { A }
func main() { var e = E.A(); match e { A => {}; A => {} } }
KR
reject 'payload arity' 'arity' <<'KR'
type E enum { A(value i64) }
func main() { var e = E.A(1); match e { A => {} } }
KR
reject 'catch fallback type' 'E110' <<'KR'
func get() !i64 { return fail(1) }
func main() { var value = get() catch false; println(value) }
KR
reject 'catch requires result' 'catch requires' <<'KR'
func main() { println(1 catch 2) }
KR
reject 'defer must return void' 'return void' <<'KR'
func value() i64 { return 1 }
func main() { defer value() }
KR
reject 'defer mutable reference retained until block exit' 'borrow' <<'KR'
func bump(value &mut i64) { *value += 1 }
func main() { var n = 1; defer bump(&mut n); println(n) }
KR
reject 'dynamic tuple index' 'constant' <<'KR'
func main() { var pair = (1, true); var index = 0; println(pair[index]) }
KR
reject 'tuple references forbidden' 'ordinary values' <<'KR'
func main() { var n = 1; var pair = (&n, 1); println(pair.1) }
KR
reject 'arena allocation requires unsafe' 'unsafe' <<'KR'
func main() { arenablock memory { var values = arenaAlloc[i64](memory, 2); println(len(values)) } }
KR
reject 'recursive enum layout' 'recursive' <<'KR'
type E enum { Loop(value E) }
func main() { var e E; match e { _ => {} } }
KR
reject 'result containing reference forbidden' 'references' <<'KR'
func get(value &i64) !&i64 { return ok(value) }
func main() { var n = 1; println(*(get(&n) catch &n)) }
KR
mkdir "$WORK/module"
cat > "$WORK/module/main.kr" <<'KR'
import "event"
func main() { var e = event.E.Value((3, true)); match e { event.E.Value(pair) => { println(pair.0, pair.1) } } }
KR
cat > "$WORK/module/event.krh" <<'KR'
type E enum { Value(pair (i64, bool)) }
KR
cat > "$WORK/module/event.kr" <<'KR'
type E enum { Value(pair (i64, bool)) }
KR
build "$WORK/module/main.kr" "$WORK/module-app"
"$WORK/module-app" > "$WORK/actual" 2> "$WORK/error" || fail 'enum module'
expect 'enum header, qualified constructor and pattern, tuple payload' '3 true'
printf '\n%s v0.3 regression checks passed.\n' "$TOTAL"
