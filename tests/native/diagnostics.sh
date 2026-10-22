#!/bin/sh
# Source locations must survive tabs, clipping, imports and moved executables.
set -eu
LC_ALL=C
export LC_ALL
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
KREMEN=${1:-"$ROOT/build/kremen"}
case "$KREMEN" in /*) ;; *) KREMEN="$(pwd)/$KREMEN" ;; esac
WORK=$(mktemp -d "${TMPDIR:-/tmp}/kremen-diagnostics.XXXXXX")
trap 'rm -rf "$WORK"' EXIT HUP INT TERM
TOTAL=0
pass() { TOTAL=$((TOTAL + 1)); printf 'ok diagnostics.%s - %s\n' "$TOTAL" "$1"; }
fail() { printf 'FAIL: %s\n' "$1" >&2; cat "$WORK/error" >&2; exit 1; }
contains() { grep -F "$1" "$WORK/error" > /dev/null || fail "missing diagnostic: $1"; }
reject() {
    status=0
    "$@" > "$WORK/output" 2> "$WORK/error" || status=$?
    [ "$status" -eq 1 ] || fail "expected diagnostic exit 1, got $status"
    if grep -E 'AddressSanitizer|UndefinedBehaviorSanitizer|runtime error: (load|store|signed integer overflow)' "$WORK/error" > /dev/null; then
        fail 'diagnostic path triggered a sanitizer'
    fi
}

printf 'func main() {\n\tvar count i64 = "many"\n\tprintln(count)\n}\n' > "$WORK/input.kr"
reject "$KREMEN" check "$WORK/input.kr"
contains 'input.kr:2:18: error[E110]: type mismatch: expected i64, got string'
contains '^~~~~~'
contains 'help: Check the value and its declared type'
awk '/2 \| / { sub(/^.*2 \| /, ""); expected=index($0,"\"many\"") }
     /\^/ { sub(/^.*\| /, ""); if(index($0,"^") != expected) exit 1; found=1 }
     END { if(!found) exit 1 }' "$WORK/error" || fail 'tab-expanded underline is misaligned'
pass 'type mismatch includes byte column, expanded tabs, full token and help'

awk 'BEGIN { print "func main() {"; for(i=0;i<6000;i++)printf " "; print "println(missing)"; print "}" }' > "$WORK/input.kr"
reject "$KREMEN" check "$WORK/input.kr"
contains 'input.kr:2:6009:'; contains '...'; contains 'missing'; contains '^~~~~~~'
[ "$(wc -c < "$WORK/error")" -lt 1500 ] || fail 'long line diagnostic was not bounded'
pass 'error beyond column 6000 is safely clipped around its token'

printf 'func main() { println("caf\303\251"); println(missing) }\n' > "$WORK/input.kr"
reject "$KREMEN" check "$WORK/input.kr"
contains 'unknown variable'; contains 'café'; contains '^~~~~~~'
awk '/1 \| / { sub(/^.*1 \| /, ""); expected=index($0,"missing")-1 }
     /\^/ { sub(/^.*\| /, ""); if(index($0,"^") != expected) exit 1; found=1 }
     END { if(!found) exit 1 }' "$WORK/error" || fail 'UTF-8 continuation bytes shifted the caret'
pass 'UTF-8 before an error does not add spurious caret cells'

printf 'func main() {\n' > "$WORK/input.kr"
reject "$KREMEN" check "$WORK/input.kr"
contains 'input.kr:2:1:'; contains 'Unterminated block'; contains '2 |'; contains '^'
pass 'end-of-file error points to an empty final line'

cat > "$WORK/input.kr" <<'KR'
func main() {
    var value = 1
    var reference = &mut value
    println(*reference)
    value = 2
}
KR
reject "$KREMEN" check "$WORK/input.kr"
contains 'input.kr:5:'; contains 'note: conflicting borrow starts here'; contains 'input.kr:3:'
pass 'borrow conflict identifies both conflicting use and loan origin'

printf 'func main() { var count = 1; var count = 2; println(count) }\n' > "$WORK/input.kr"
reject "$KREMEN" check "$WORK/input.kr"
contains 'error[E108]'; contains 'note: previous declaration is here'
pass 'shadowing identifies the previous local declaration'

reject "$KREMEN" fmt "$WORK/missing.kr"
contains "$WORK/missing.kr:1:1: error[E010]"
pass 'source-open error names its actual path'

mkdir "$WORK/math"
printf 'func crash(index i64) {\n    var values = [2]i64{4, 5}\n    println(values[index])\n}\n' > "$WORK/math/fault.kr"
printf 'import "math/fault"\nfunc main() { defer println("cleanup"); fault.crash(9) }\n' > "$WORK/main.kr"
"$KREMEN" build "$WORK/main.kr" -O0 -o "$WORK/program" > "$WORK/output" 2> "$WORK/error" || fail 'build imported runtime fixture'
mv "$WORK/math/fault.kr" "$WORK/hidden-source"
mv "$WORK/program" "$WORK/moved-program"
reject "$WORK/moved-program"
contains 'math/fault.kr:3:13: runtime error: array index out of bounds'
contains '3 |     println(values[index])'; contains '^~~~~~'
grep -Fx 'cleanup' "$WORK/output" > /dev/null || fail 'runtime diagnostic skipped defer'
pass 'moved program reports imported file and embedded excerpt, then runs defer'

printf 'func main() {\n    var value = readInt()\n    println(value + 1)\n}\n' > "$WORK/input.kr"
"$KREMEN" build "$WORK/input.kr" -O3 -o "$WORK/program" > "$WORK/output" 2> "$WORK/error" || fail 'build overflow fixture'
printf '9223372036854775807\n' > "$WORK/stdin"
reject "$WORK/program" < "$WORK/stdin"
contains 'input.kr:3:19: runtime error: integer overflow'; contains 'println(value + 1)'
pass 'optimized arithmetic reports the Kremen operator location'

printf 'func main() {\n    defer println("done")\n    panic("stopped here")\n}\n' > "$WORK/input.kr"
"$KREMEN" build "$WORK/input.kr" -o "$WORK/program" > "$WORK/output" 2> "$WORK/error" || fail 'build panic fixture'
reject "$WORK/program"
contains 'input.kr:3:5: runtime error: stopped here'; contains 'panic("stopped here")'
grep -Fx 'done' "$WORK/output" > /dev/null || fail 'panic diagnostic skipped defer'
pass 'user panic retains its location and cleanup'

cat > "$WORK/input.kr" <<'KR'
func main() {
    unsafe {
        var values = alloc[i64](1)
        defer free(values)
        free(values)
    }
}
KR
"$KREMEN" build "$WORK/input.kr" -o "$WORK/program" > "$WORK/output" 2> "$WORK/error" || fail 'build deferred-check fixture'
reject "$WORK/program"
contains 'input.kr:4:15: runtime error:'; contains 'defer free(values)'
pass 'a failed deferred operation retains its registration source location'

printf 'func broken() !i64 { return fail(7) }\nfunc main() { var result = broken(); println(result.value) }\n' > "$WORK/input.kr"
"$KREMEN" build "$WORK/input.kr" -o "$WORK/program" > "$WORK/output" 2> "$WORK/error" || fail 'build failed-result fixture'
reject "$WORK/program"
contains 'input.kr:2:'; contains 'runtime error: access to failed result'; contains 'result.value'
pass 'failed-result access identifies its source expression'

printf 'func main() { println("hello") }\n' > "$WORK/input.kr"
"$KREMEN" emit-c "$WORK/input.kr" -o "$WORK/standalone.c" > "$WORK/output" 2> "$WORK/error" || fail 'emit no-check program'
"${CC:-cc}" -std=c11 -Wall -Wextra -Wpedantic -Werror "$WORK/standalone.c" -o "$WORK/program" 2> "$WORK/error" || fail 'strict C without any location entries'
"$WORK/program" > "$WORK/output"
grep -Fx 'hello' "$WORK/output" > /dev/null || fail 'standalone C changed behavior'
pass 'programs without checked operations still compile as self-contained strict C'

printf 'Native diagnostics passed: %s checks.\n' "$TOTAL"
