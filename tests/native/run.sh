#!/bin/sh
# Native compiler integration tests. No Python or third-party test runner.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
KREMEN=${1:-"$ROOT/build/kremen"}
case "$KREMEN" in
    /*) ;;
    *) KREMEN="$(pwd)/$KREMEN" ;;
esac
if [ ! -x "$KREMEN" ]; then
    printf 'Missing native compiler: %s\nRun make first.\n' "$KREMEN" >&2
    exit 1
fi
WORK=$(mktemp -d "${TMPDIR:-/tmp}/kremen-native-tests.XXXXXX")
trap 'rm -rf "$WORK"' EXIT HUP INT TERM
FIXTURES="$ROOT/tests/native"
TOTAL=0

pass() {
    TOTAL=$((TOTAL + 1))
    printf 'ok %s - %s\n' "$TOTAL" "$1"
}

fail() {
    printf 'FAIL: %s\n' "$1" >&2
    if [ -s "$WORK/err" ]; then cat "$WORK/err" >&2; fi
    exit 1
}

expect_output() {
    name=$1
    expected=$2
    actual=$3
    printf '%s\n' "$expected" > "$WORK/expected"
    if ! cmp -s "$WORK/expected" "$actual"; then
        diff -u "$WORK/expected" "$actual" >&2 || :
        fail "$name: unexpected output"
    fi
    pass "$name"
}

build_fixture() {
    "$KREMEN" build "$FIXTURES/$1.kr" -o "$WORK/$1" > "$WORK/out" 2> "$WORK/err" || fail "build $1"
}

reject_source() {
    name=$1
    diagnostic=$2
    cat > "$WORK/invalid.kr"
    if "$KREMEN" check "$WORK/invalid.kr" > "$WORK/out" 2> "$WORK/err"; then
        fail "$name: invalid program accepted"
    fi
    if ! grep -F "$diagnostic" "$WORK/err" > /dev/null; then
        fail "$name: expected diagnostic $diagnostic"
    fi
    grep -F 'invalid.kr:' "$WORK/err" > /dev/null || fail "$name: no source location"
    grep -F '^' "$WORK/err" > /dev/null || fail "$name: no diagnostic caret"
    pass "$name"
}

for example in hello demo references unsafe io; do
    "$KREMEN" check "$ROOT/examples/$example.kr" > "$WORK/out" 2> "$WORK/err" || fail "check example $example"
done
pass 'all original examples typecheck'

build_fixture values
"$WORK/values" > "$WORK/out" 2> "$WORK/err" || fail 'run values'
expect_output 'types, methods, arrays, borrows, short circuit, evaluation order' '14 4
2 99
false true 0
12
5
6
-7 18446744073709551614 128
Привет 12' "$WORK/out"

build_fixture results
"$WORK/results" > "$WORK/out" 2> "$WORK/err" || fail 'run results'
expect_output 'result success and propagation' 'true 41
false 17' "$WORK/out"

build_fixture input_result
for entry in valid eof format range; do
    case "$entry" in
        valid) printf '%s\n' '-9223372036854775808' > "$WORK/input"; expected=-9223372036854775808 ;;
        eof) : > "$WORK/input"; expected='error 1' ;;
        format) printf '12x\n' > "$WORK/input"; expected='error 2' ;;
        range) printf '9223372036854775808\n' > "$WORK/input"; expected='error 3' ;;
    esac
    "$WORK/input_result" < "$WORK/input" > "$WORK/out" 2> "$WORK/err" || fail "result input $entry"
    expect_output "result input $entry" "$expected" "$WORK/out"
done

build_fixture slices
"$WORK/slices" > "$WORK/out" 2> "$WORK/err" || fail 'run slices'
expect_output 'zero-initialized allocation, slice aliases and empty view' '4 0 0
2 19 11 19
0' "$WORK/out"

"$KREMEN" build "$FIXTURES/modules/main.kr" -o "$WORK/modules" > "$WORK/out" 2> "$WORK/err" || fail 'build local modules'
"$WORK/modules" > "$WORK/out" 2> "$WORK/err" || fail 'run local modules'
expect_output 'directory module, file module, alias and API header' '7 15' "$WORK/out"

build_fixture traps
for safety_case in \
    '1|integer overflow' \
    '2|division by zero' \
    '3|array index out of bounds' \
    '4|access to failed result' \
    '5|slice use after free' \
    '6|slice use after free' \
    '7|slice double free' \
    '8|array index out of bounds' \
    '9|slice bounds out of range' \
    '10|negative slice allocation length' \
    '11|cannot free a partial slice view' \
    '12|failure code must be positive' \
    '13|failure code must be positive'; do
    mode=${safety_case%%|*}
    diagnostic=${safety_case#*|}
    printf '%s\n' "$mode" > "$WORK/input"
    if "$WORK/traps" < "$WORK/input" > "$WORK/out" 2> "$WORK/err"; then
        fail "runtime safety case $mode did not trap"
    fi
    grep -F 'runtime error at line ' "$WORK/err" > /dev/null || fail "runtime safety case $mode has no location"
    grep -F "$diagnostic" "$WORK/err" > /dev/null || fail "runtime safety case $mode has incorrect diagnostic"
    pass "runtime safety case $mode: $diagnostic"
done

build_fixture memory_order
printf '0\n' > "$WORK/input"
"$WORK/memory_order" < "$WORK/input" > "$WORK/out" 2> "$WORK/err" || fail 'slice lvalue evaluation order'
expect_output 'slice lvalue captures descriptor and evaluates index once' '2 9 0 0
17' "$WORK/out"
for mode in 1 2 3 4 5 6; do
    printf '%s\n' "$mode" > "$WORK/input"
    if "$WORK/memory_order" < "$WORK/input" > "$WORK/out" 2> "$WORK/err"; then
        fail "lvalue lifetime case $mode did not trap"
    fi
    grep -F 'runtime error at line ' "$WORK/err" > /dev/null || fail "lvalue lifetime case $mode has no source location"
    grep -F 'slice use after free' "$WORK/err" > /dev/null || fail "lvalue lifetime case $mode has incorrect diagnostic"
    pass "slice lvalue lifetime check $mode"
done

# CC may contain a compiler command and options, as in the Makefile.
${CC:-cc} -std=c11 -Wall -Wextra -Wpedantic -Werror -O2 "$FIXTURES/runtime.c" -o "$WORK/runtime" > "$WORK/out" 2> "$WORK/err" || fail 'compile runtime invariant tests'
"$WORK/runtime" > "$WORK/out" 2> "$WORK/err" || fail 'runtime empty and copied slice invariants'
printf '%s\n' '0 -9223372036854775808 9223372036854775807 12x 9223372036854775808 - 2' > "$WORK/input"
"$WORK/runtime" input < "$WORK/input" > "$WORK/out" 2> "$WORK/err" || fail 'runtime full integer input cases'
"$WORK/runtime" ioerror > "$WORK/out" 2> "$WORK/err" || fail 'runtime recoverable I/O error'
pass 'runtime zero descriptors and complete result input error codes'
for mode in overflow zeroindex mismatch reverse uafview; do
    if "$WORK/runtime" "$mode" > "$WORK/out" 2> "$WORK/err"; then fail "runtime invariant $mode did not trap"; fi
    grep -F 'runtime error' "$WORK/err" > /dev/null || fail "runtime invariant $mode has no diagnostic"
    pass "runtime invariant $mode"
done

reject_source 'strict integer typing' 'E110' <<'KR'
func main() {
    var small i8 = 1
    var large i64 = small
    println(large)
}
KR
reject_source 'warning is an error' 'W001' <<'KR'
func main() {
    var unused = 1
}
KR
reject_source 'allocation requires unsafe' 'unsafe' <<'KR'
func main() {
    var values = alloc[i64](2)
    println(len(values))
}
KR
reject_source 'free requires unsafe' 'unsafe' <<'KR'
func main() {
    unsafe {
        var values = alloc[i64](2)
        release(values)
    }
}
func release(values []i64) {
    free(values)
}
KR
reject_source 'propagation requires result return' '?' <<'KR'
func main() {
    var number = readIntResult()?
    println(number)
}
KR
reject_source 'mutable borrow rejects alias read' 'borrow' <<'KR'
func main() {
    var number = 1
    var reference = &mut number
    println(number, *reference)
}
KR
reject_source 'raw pointer preserves borrowing restrictions' 'borrow' <<'KR'
func main() {
    var number = 1
    unsafe {
        var pointer = raw(&mut number)
        println(number, *pointer)
    }
}
KR
reject_source 'zero failure code is rejected' 'E119' <<'KR'
func number() !i64 { return fail(0) }
func main() { var result = number(); println(result.ok) }
KR
reject_source 'negative failure code is rejected' 'E119' <<'KR'
func number() !i64 { return fail(-1) }
func main() { var result = number(); println(result.ok) }
KR
reject_source 'propagation requires a result expression' 'E119' <<'KR'
func number() !i64 { var value = 1?; return ok(value) }
func main() { var result = number(); println(result.ok) }
KR
reject_source 'result requires explicit initialization' 'E116' <<'KR'
func main() {
    var result !i64
    println(result.ok)
}
KR
reject_source 'aggregate result requires explicit initialization' 'E116' <<'KR'
type Response struct { result !i64 }
func main() {
    var response Response
    println(response.result.ok)
}
KR
reject_source 'zero-filled allocation cannot contain an uninitialized result' 'E130' <<'KR'
func main() {
    unsafe {
        var values = alloc[!i64](2)
        println(len(values))
        free(values)
    }
}
KR

cat > "$WORK/header.krh" <<'KR'
type Value struct { number i64 }
func get(value i64) i64
KR
cat > "$WORK/header.kr" <<'KR'
type Value struct { number bool }
func get(value i64) i64 { return value }
func main() { println(get(1)) }
KR
if "$KREMEN" check "$WORK/header.kr" > "$WORK/out" 2> "$WORK/err"; then fail 'conflicting header struct accepted'; fi
grep -F 'E110' "$WORK/err" > /dev/null || fail 'header struct mismatch diagnostic'
pass 'API header struct mismatch rejected'
cat > "$WORK/header.kr" <<'KR'
type Value struct { number i64 }
func get(value bool) i64 { if value { return 1 }; return 0 }
func main() { println(get(true)) }
KR
if "$KREMEN" check "$WORK/header.kr" > "$WORK/out" 2> "$WORK/err"; then fail 'conflicting header signature accepted'; fi
grep -F 'E110' "$WORK/err" > /dev/null || fail 'header signature mismatch diagnostic'
pass 'API header function mismatch rejected'

mkdir -p "$WORK/cycle/a"
cat > "$WORK/cycle/main.kr" <<'KR'
import "a"
func main() { println(a.value()) }
KR
cat > "$WORK/cycle/a/a.kr" <<'KR'
import ".."
func value() i64 { return 1 }
KR
if "$KREMEN" check "$WORK/cycle/main.kr" > "$WORK/out" 2> "$WORK/err"; then fail 'import cycle accepted'; fi
[ -s "$WORK/err" ] || fail 'import cycle has no diagnostic'
pass 'cyclic imports rejected'

mkdir -p "$WORK/confined"
cat > "$WORK/confined/main.kr" <<'KR'
import "../external"
func main() { println(external.value()) }
KR
cat > "$WORK/external.kr" <<'KR'
func value() i64 { return 1 }
KR
if "$KREMEN" check "$WORK/confined/main.kr" > "$WORK/out" 2> "$WORK/err"; then fail 'outside-project import accepted'; fi
[ -s "$WORK/err" ] || fail 'outside-project import has no diagnostic'
pass 'outside-project imports rejected'

ln -s "$WORK/external.kr" "$WORK/confined/escape.kr"
cat > "$WORK/confined/main.kr" <<'KR'
import "escape"
func main() { println(escape.value()) }
KR
if "$KREMEN" check "$WORK/confined/main.kr" > "$WORK/out" 2> "$WORK/err"; then fail 'outside-project symlink accepted'; fi
[ -s "$WORK/err" ] || fail 'outside-project symlink has no diagnostic'
pass 'imports through symlinks respect project root'

cat > "$WORK/format.kr" <<'KR'
// retained leading comment
package main
func main(){var n i64=2 // retained trailing comment
/* retained block comment */
if n>1{println("spacing // inside string",n)}else{println(0)}}
KR
if "$KREMEN" fmt --check "$WORK/format.kr" > "$WORK/out" 2> "$WORK/err"; then fail 'fmt --check accepted unformatted file'; fi
"$KREMEN" fmt "$WORK/format.kr" > "$WORK/formatted.kr" 2> "$WORK/err" || fail 'fmt stdout'
"$KREMEN" fmt "$WORK/formatted.kr" > "$WORK/formatted-again.kr" 2> "$WORK/err" || fail 'fmt idempotence'
cmp -s "$WORK/formatted.kr" "$WORK/formatted-again.kr" || fail 'formatter is not idempotent'
for comment in 'retained leading comment' 'retained trailing comment' 'retained block comment' 'spacing // inside string'; do
    grep -F "$comment" "$WORK/formatted.kr" > /dev/null || fail "formatter lost $comment"
done
"$KREMEN" fmt -w "$WORK/format.kr" > "$WORK/out" 2> "$WORK/err" || fail 'fmt -w'
cmp -s "$WORK/format.kr" "$WORK/formatted.kr" || fail 'fmt -w differs from fmt stdout'
"$KREMEN" fmt --check "$WORK/format.kr" > "$WORK/out" 2> "$WORK/err" || fail 'fmt --check canonical source'
"$KREMEN" run "$WORK/format.kr" > "$WORK/out" 2> "$WORK/err" || fail 'run formatted source'
expect_output 'formatter preserves comments, is idempotent, supports check and write' 'spacing // inside string 2' "$WORK/out"

"$KREMEN" run "$FIXTURES/format_composites.kr" > "$WORK/composites-before" 2> "$WORK/err" || fail 'run composite source before formatting'
"$KREMEN" fmt "$FIXTURES/format_composites.kr" > "$WORK/composites.kr" 2> "$WORK/err" || fail 'format composite literals'
"$KREMEN" fmt "$WORK/composites.kr" > "$WORK/composites-again.kr" 2> "$WORK/err" || fail 'reformat composite literals'
cmp -s "$WORK/composites.kr" "$WORK/composites-again.kr" || fail 'composite formatting is not idempotent'
grep -F 'retain element comment' "$WORK/composites.kr" > /dev/null || fail 'composite element comment lost'
grep -F 'retain result comment' "$WORK/composites.kr" > /dev/null || fail 'composite result comment lost'
"$KREMEN" run "$WORK/composites.kr" > "$WORK/composites-after" 2> "$WORK/err" || fail 'run composite source after formatting'
cmp -s "$WORK/composites-before" "$WORK/composites-after" || fail 'formatting changed composite source behavior'
expect_output 'formatter composite arrays, nested arrays, results and condition literals' '2 3
true' "$WORK/composites-after"

CC=kremen-nonexistent-c-compiler "$KREMEN" check "$FIXTURES/results.kr" > "$WORK/out" 2> "$WORK/err" || fail 'check depends on C compiler'
CC=kremen-nonexistent-c-compiler "$KREMEN" emit-c "$FIXTURES/results.kr" -o "$WORK/results.c" > "$WORK/out" 2> "$WORK/err" || fail 'emit-c depends on C compiler'
[ -s "$WORK/results.c" ] || fail 'empty emitted C'
pass 'check and emit-c work without an external C compiler'

cp "$FIXTURES/results.kr" "$WORK/preserve.kr"
cp "$WORK/preserve.kr" "$WORK/original.kr"
if "$KREMEN" emit-c "$WORK/preserve.kr" -o "$WORK/preserve.kr" > "$WORK/out" 2> "$WORK/err"; then fail 'emit-c can overwrite source'; fi
cmp -s "$WORK/preserve.kr" "$WORK/original.kr" || fail 'source overwritten'
printf 'existing artifact\n' > "$WORK/existing"
printf 'func main() { println(unknown) }\n' > "$WORK/bad.kr"
if "$KREMEN" build "$WORK/bad.kr" -o "$WORK/existing" > "$WORK/out" 2> "$WORK/err"; then fail 'invalid build succeeded'; fi
expect_output 'failed build preserves previous output' 'existing artifact' "$WORK/existing"

"$KREMEN" build "$FIXTURES/results.kr" -O0 -o "$WORK/unoptimized" > "$WORK/out" 2> "$WORK/err" || fail 'build -O0'
"$WORK/unoptimized" > "$WORK/out" 2> "$WORK/err" || fail 'run -O0'
expect_output '-O0 retains result semantics' 'true 41
false 17' "$WORK/out"

printf '\n%s native integration checks passed.\n' "$TOTAL"
