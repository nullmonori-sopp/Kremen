#!/bin/sh
# Bounded malformed inputs and format preservation at token/comment boundaries.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
KREMEN=${1:-"$ROOT/build/kremen"}
case "$KREMEN" in /*) ;; *) KREMEN="$(pwd)/$KREMEN" ;; esac
WORK=$(mktemp -d "${TMPDIR:-/tmp}/kremen-frontend-stability.XXXXXX")
trap 'rm -rf "$WORK"' EXIT HUP INT TERM
TOTAL=0
pass() { TOTAL=$((TOTAL + 1)); printf 'ok frontend.%s - %s\n' "$TOTAL" "$1"; }
fail() { printf 'FAIL: %s\n' "$1" >&2; cat "$WORK/error" >&2; exit 1; }
reject() {
    name=$1; diagnostic=$2
    cat > "$WORK/invalid.kr"
    status=0
    "$KREMEN" check "$WORK/invalid.kr" > "$WORK/output" 2> "$WORK/error" || status=$?
    [ "$status" -eq 1 ] || fail "$name: expected diagnostic exit 1, got $status"
    grep -F "$diagnostic" "$WORK/error" > /dev/null || fail "$name: wrong diagnostic"
    grep -F 'invalid.kr:' "$WORK/error" > /dev/null || fail "$name: no source location"
    grep -F '^' "$WORK/error" > /dev/null || fail "$name: no caret"
    pass "$name"
}
cat > "$WORK/source.kr" <<'KR'
type State enum { Ready(pair (i64, bool)); Empty }
func checked(number i64) !i64 {
    if number < 0 { return fail(7) }
    return ok(number)
}
func main() {
    var value = checked(-1) catch /* fallback binding */ code =>
        code + 1
    for i := 0; i < 3; i++ {
        if i == 0 { println("zero") } /* first else gap */ /* second gap */ else /* else-if gap */ if i == 1 {
            println(value)
        } /* final else gap */ else { println("other") }
    }
    var state = State.Ready((value, true))
    match state {
        State.Ready(pair) => { var (number, accepted) = pair; println(number, accepted) }
        Empty => { println("empty") }
    }
}
KR
"$KREMEN" build "$WORK/source.kr" -O0 -o "$WORK/before" > "$WORK/output" 2> "$WORK/error" || fail 'build original formatter fixture'
"$WORK/before" > "$WORK/before.output" 2> "$WORK/error" || fail 'run original formatter fixture'
"$KREMEN" fmt "$WORK/source.kr" > "$WORK/formatted.kr" 2> "$WORK/error" || fail 'format inline-comment else branches'
"$KREMEN" fmt "$WORK/formatted.kr" > "$WORK/again.kr" 2> "$WORK/error" || fail 'repeat formatter'
cmp -s "$WORK/formatted.kr" "$WORK/again.kr" || fail 'formatter not idempotent'
"$KREMEN" fmt --check "$WORK/formatted.kr" > "$WORK/output" 2> "$WORK/error" || fail 'formatter check'
for comment in 'fallback binding' 'first else gap' 'second gap' 'else-if gap' 'final else gap'; do
    grep -F "$comment" "$WORK/formatted.kr" > /dev/null || fail 'formatter lost comment'
done
"$KREMEN" build "$WORK/formatted.kr" -O0 -o "$WORK/after" > "$WORK/output" 2> "$WORK/error" || fail 'build formatted fixture'
"$WORK/after" > "$WORK/after.output" 2> "$WORK/error" || fail 'run formatted fixture'
cmp -s "$WORK/before.output" "$WORK/after.output" || fail 'formatting changed program behavior'
printf 'zero\n8\nother\n8 true\n' > "$WORK/expected"
cmp -s "$WORK/expected" "$WORK/after.output" || fail 'unexpected formatter fixture output'
pass 'inline comments before else/else-if, multiline catch, matching and tuples preserve behavior'

awk 'BEGIN { printf "func main() { println("; for(i=0;i<400;i++)printf "("; printf "1"; for(i=0;i<400;i++)printf ")"; print ") }" }' > "$WORK/generated.kr"
reject 'deep parenthesized expression reports a limit' 'E111' < "$WORK/generated.kr"
awk 'BEGIN { printf "func main() { println("; for(i=0;i<400;i++)printf "!"; print "true) }" }' > "$WORK/generated.kr"
reject 'deep unary expression reports a limit' 'E111' < "$WORK/generated.kr"
awk 'BEGIN { printf "func main() { println("; for(i=0;i<2000;i++)printf "1 + "; print "1) }" }' > "$WORK/generated.kr"
reject 'left-associated arithmetic chain reports an AST limit' 'E111' < "$WORK/generated.kr"
awk 'BEGIN { printf "func main() { var value = 1; println(value"; for(i=0;i<2000;i++)printf ".field"; print ") }" }' > "$WORK/generated.kr"
reject 'postfix field chain reports an AST limit' 'E111' < "$WORK/generated.kr"
awk 'BEGIN { printf "func main() { var value "; for(i=0;i<400;i++)printf "[1]"; print "i64; println(value) }" }' > "$WORK/generated.kr"
reject 'deep type annotation reports a limit' 'E111' < "$WORK/generated.kr"
awk 'BEGIN { printf "func main() { "; for(i=0;i<400;i++)printf "if true {} else "; print "{} }" }' > "$WORK/generated.kr"
reject 'long else-if chain reports a limit' 'E111' < "$WORK/generated.kr"
awk 'BEGIN { printf "func main() {"; for(i=0;i<1000010;i++)printf ";"; print "}" }' > "$WORK/generated.kr"
reject 'dense source reports the token budget before exhausting memory' '1000000 tokens' < "$WORK/generated.kr"
printf 'func main() { println("\377") }\n' > "$WORK/generated.kr"
reject 'invalid UTF-8 in a string is diagnosed' 'Invalid UTF-8' < "$WORK/generated.kr"
reject 'surrogate escape is diagnosed' 'Unicode scalar' <<'KR'
func main() { println("\ud800") }
KR
reject 'unterminated enum payload is diagnosed' 'E110' <<'KR'
type E enum { Value(number i64
KR
reject 'missing catch replacement is diagnosed' 'E110' <<'KR'
func main() { println(1 catch code => ) }
KR
reject 'unterminated match body is diagnosed' 'E110' <<'KR'
type E enum { A }
func main() { var e = E.A(); match e { A => {
KR
printf '\n%s frontend stability checks passed.\n' "$TOTAL"
