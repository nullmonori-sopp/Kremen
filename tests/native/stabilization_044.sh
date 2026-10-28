#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
KREMEN=${1:-"$ROOT/build/kremen"}
case "$KREMEN" in /*) ;; *) KREMEN="$(pwd)/$KREMEN" ;; esac
WORK=$(mktemp -d "${TMPDIR:-/tmp}/kremen-stabilization044.XXXXXX")
trap 'rm -rf "$WORK"' EXIT HUP INT TERM
TOTAL=0
pass() { TOTAL=$((TOTAL + 1)); printf 'ok stabilization044.%s - %s\n' "$TOTAL" "$1"; }
fail() { printf 'FAIL: %s\n' "$1" >&2; cat "$WORK/error" >&2; exit 1; }
json_error() {
    label=$1; code=$2; shift 2
    status=0
    "$KREMEN" "$@" --diagnostic-format=json > "$WORK/output" 2> "$WORK/error" || status=$?
    [ "$status" -eq 1 ] || fail "$label status $status"
    [ ! -s "$WORK/output" ] || fail "$label wrote to stdout"
    "$WORK/json-validate" "$WORK/error" || fail "$label invalid JSON"
    grep -F "\"code\":\"$code\"" "$WORK/error" > /dev/null || fail "$label code"
    grep -F "\"explain\":\"kremen explain $code\"" "$WORK/error" > /dev/null || fail "$label explanation command"
    pass "$label"
}
CC_TOOL=${CC:-cc}
"$CC_TOOL" -std=c11 -Wall -Wextra -Wpedantic -Werror "$ROOT/tests/native/json_validate.c" -o "$WORK/json-validate"
if [ "${SANITIZE_GENERATED:-0}" = 1 ]; then
    "$CC_TOOL" -std=c11 -Wall -Wextra -Wpedantic -Werror -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all \
        "$ROOT/tests/native/compiler_core_044.c" "$ROOT/native/core.c" "$ROOT/native/diagnostics.c" "$ROOT/native/fast_copy.c" -o "$WORK/core"
else
    "$CC_TOOL" -std=c11 -Wall -Wextra -Wpedantic -Werror -O2 \
        "$ROOT/tests/native/compiler_core_044.c" "$ROOT/native/core.c" "$ROOT/native/diagnostics.c" "$ROOT/native/fast_copy.c" -o "$WORK/core"
fi
"$WORK/core" > "$WORK/output" 2> "$WORK/error" || fail 'compiler formatting'
pass 'short/long formatting preserves variadic arguments, boundaries, bytes and terminators'

"$KREMEN" explain --list > "$WORK/catalog" 2> "$WORK/error" || fail 'list explanations'
[ ! -s "$WORK/error" ] || fail 'catalog stderr'
awk '/^[EW][0-9][0-9][0-9]  / { print $1 }' "$WORK/catalog" > "$WORK/codes"
grep -oh '"[EW][0-9][0-9][0-9]"' "$ROOT"/native/*.c | tr -d '"' | LC_ALL=C sort -u > "$WORK/emitted-codes"
cmp -s "$WORK/emitted-codes" "$WORK/codes" || fail 'explanation catalog coverage and order'
pass 'catalog covers every native diagnostic code in stable order'
while IFS= read -r code; do
    "$KREMEN" explain "$code" > "$WORK/output" 2> "$WORK/error" || fail "$code explanation"
    [ ! -s "$WORK/error" ] || fail "$code explanation stderr"
    grep -F "$code:" "$WORK/output" > /dev/null || fail "$code heading"
    for section in 'Why this happens' 'What to check' 'Example'; do
        grep -Fx "$section" "$WORK/output" > /dev/null || fail "$code missing $section"
    done
done < "$WORK/codes"
pass 'every diagnostic explanation has a reason, remedy and example'
"$KREMEN" explain > "$WORK/output" 2> "$WORK/error" || fail 'default catalog'
cmp -s "$WORK/catalog" "$WORK/output" || fail 'default catalog output'
pass 'explain without arguments lists the catalog'
(cd "$WORK" && CC=/nonexistent/c-compiler "$KREMEN" explain E131) > "$WORK/output" 2> "$WORK/error" || fail 'offline explanation'
grep -F 'lexical block' "$WORK/output" > /dev/null || fail 'borrow explanation content'
pass 'explanations work outside a project without invoking a C compiler'
"$KREMEN" explain E110 --diagnostic-format=json > "$WORK/output" 2> "$WORK/error" || fail 'explain with renderer'
[ ! -s "$WORK/error" ] || fail 'explain with renderer stderr'
grep -F 'Kremen does not implicitly convert' "$WORK/output" > /dev/null || fail 'explanation text'
pass 'diagnostic rendering option leaves successful explanation on stdout'
json_error 'unknown diagnostic is a structured CLI error' E001 explain E999
json_error 'extra explanation arguments are rejected' E001 explain E110 E131
json_error 'list cannot be combined with a code' E001 explain --list E110
json_error 'explain rejects compilation options' E001 explain E110 -O3
json_error 'unknown command is a structured error' E001 chcek
json_error 'missing source is a structured error' E001 check
json_error 'multiple sources are a structured error' E001 check first.kr second.kr
json_error 'conflicting formatter modes are structured' E001 fmt --check -w
json_error 'formatter write requires a path' E001 fmt -w
json_error 'formatter root option is rejected' E001 fmt --root "$WORK"
printf 'func main() {}\n' > "$WORK/source.txt"
json_error 'formatter extension errors carry a source path' E012 fmt "$WORK/source.txt"
grep -F 'source.txt' "$WORK/error" > /dev/null || fail 'extension path'
printf 'func main() {\r\n\t\000}\n' > "$WORK/input"
json_error 'formatter stdin NUL uses source-aware JSON' E100 fmt < "$WORK/input"
grep -F '"file":"<stdin>","line":2,"column":2' "$WORK/error" > /dev/null || fail 'stdin NUL location'
awk 'BEGIN { for(i=0;i<4097;i++) printf "%01024d",0 }' > "$WORK/oversized"
json_error 'formatter oversized stdin is structured' E010 fmt < "$WORK/oversized"
printf 'func main() {}\n' > "$WORK/valid.kr"
json_error 'emit protects its source with a structured diagnostic' E001 emit-c "$WORK/valid.kr" -o "$WORK/valid.kr"
grep -F 'func main() {}' "$WORK/valid.kr" > /dev/null || fail 'protected source changed'
json_error 'build protects its source with a structured diagnostic' E001 build "$WORK/valid.kr" -o "$WORK/valid.kr"

printf 'func main() { var amount bol = true; println(amount) }\n' > "$WORK/invalid.kr"
json_error 'unknown type underlines its type token' E105 check "$WORK/invalid.kr"
grep -F '"column":26,"end_line":1,"end_column":29,"offset":25,"length":3' "$WORK/error" > /dev/null || fail 'type range'
mkdir "$WORK/math"
cat > "$WORK/math/types.kr" <<'KR'
type Vector struct { number i64 }
func tiny() i8 { println("imported"); return 7 }
func (v Vector) tiny() i8 { println("method"); return i8(v.number) }
func (v &Vector) borrowed() i8 { println("borrowed"); return i8(v.number) }
func make() Vector { println("make"); return Vector{number: 8} }
KR
printf 'import "math/types"\nfunc main() { var sample types.Vectro; println(sample.number) }\n' > "$WORK/invalid.kr"
json_error 'qualified type typo searches the imported module' E105 check "$WORK/invalid.kr"
grep -F "Did you mean 'types.Vector'?" "$WORK/error" > /dev/null || fail 'qualified suggestion'
grep -F '"column":26,"end_line":2,"end_column":38' "$WORK/error" > /dev/null || fail 'qualified type range'
printf 'import "math/types"\nfunc main() { var sample types.bol; println(sample) }\n' > "$WORK/invalid.kr"
json_error 'qualified type typo does not suggest a nonexistent qualified primitive' E105 check "$WORK/invalid.kr"
if grep -F 'Did you mean' "$WORK/error" > /dev/null; then fail 'qualified primitive suggestion'; fi

cat > "$WORK/positive.kr" <<'KR'
import "math/types"
func take(value i8) { println(value) }
func main() {
    var imported = 1 + types.tiny()
    take(imported)
    var vector = types.Vector{number: 9}
    var method = 1 + vector.tiny()
    take(method)
    var nested = 1 + types.make().tiny()
    take(nested)
    var reference = &vector
    var borrowed = 1 + reference.borrowed()
    take(borrowed)
    var subtracted = 20 - types.tiny()
    take(subtracted)
    println(1 < types.tiny())
}
KR
"$KREMEN" emit-c "$WORK/positive.kr" -o "$WORK/positive.c" > "$WORK/output" 2> "$WORK/error" || fail 'context inference emission'
if [ "${SANITIZE_GENERATED:-0}" = 1 ]; then
    "$CC_TOOL" -std=c11 -pthread -Wall -Wextra -Wpedantic -Werror -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all \
        "$WORK/positive.c" -o "$WORK/positive" 2> "$WORK/error" || fail 'context inference sanitized C'
else
    "$CC_TOOL" -std=c11 -pthread -Wall -Wextra -Wpedantic -Werror -O3 "$WORK/positive.c" -o "$WORK/positive" 2> "$WORK/error" || fail 'context inference C'
fi
"$WORK/positive" > "$WORK/output" 2> "$WORK/error" || fail 'context inference execution'
printf 'imported\n8\nmethod\n10\nmake\nmethod\n9\nborrowed\n10\nimported\n13\nimported\ntrue\n' > "$WORK/expected"
cmp -s "$WORK/expected" "$WORK/output" || fail 'context inference output/order'
pass 'imported, value, borrowed and nested methods infer narrow integers and execute once'
printf 'import "math/types"\nfunc main() { println(128 + types.tiny()) }\n' > "$WORK/invalid.kr"
json_error 'context inference keeps literal range checks' E123 check "$WORK/invalid.kr"
printf 'import "math/types"\nfunc main() { var wide i64 = 1; println(wide + types.tiny()) }\n' > "$WORK/invalid.kr"
json_error 'context inference does not add implicit integer conversion' E110 check "$WORK/invalid.kr"
printf 'import "math/types"\nfunc main() { println(1 + types.tniy()) }\n' > "$WORK/invalid.kr"
json_error 'inference retains precise imported-function errors' E107 check "$WORK/invalid.kr"
grep -F "Did you mean 'tiny'?" "$WORK/error" > /dev/null || fail 'call typo suggestion'
"$KREMEN" check "$WORK/valid.kr" --diagnostic-format=json > "$WORK/output" 2> "$WORK/error" || fail 'successful check'
[ ! -s "$WORK/error" ] || fail 'successful check diagnostics'
pass 'successful checks keep an empty diagnostic stream'
printf 'Stabilization 0.4.4 passed: %s checks.\n' "$TOTAL"
