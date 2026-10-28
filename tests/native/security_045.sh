#!/bin/sh
set -eu
LC_ALL=C
export LC_ALL
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
KREMEN=${1:-"$ROOT/build/kremen"}
case "$KREMEN" in /*) ;; *) KREMEN="$(pwd)/$KREMEN" ;; esac
WORK=$(mktemp -d "${TMPDIR:-/tmp}/kremen-security045.XXXXXX")
trap 'rm -rf "$WORK"' EXIT HUP INT TERM
TOTAL=0
pass() { TOTAL=$((TOTAL + 1)); printf 'ok security045.%s - %s\n' "$TOTAL" "$1"; }
fail() { printf 'FAIL: %s\n' "$1" >&2; od -An -tx1 "$WORK/error" >&2; exit 1; }
reject() {
    label=$1; code=$2; shift 2
    status=0
    timeout 10 "$KREMEN" "$@" --diagnostic-format=json > "$WORK/output" 2> "$WORK/error" || status=$?
    [ "$status" -eq 1 ] || fail "$label status $status"
    "$WORK/json" "$WORK/error" || fail "$label JSON"
    grep -F "\"code\":\"$code\"" "$WORK/error" > /dev/null || fail "$label code"
    pass "$label"
}
compile() {
    if [ "${SANITIZE_GENERATED:-0}" = 1 ]; then
        "${CC:-cc}" -std=c11 -pthread -Wall -Wextra -Wpedantic -Werror -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all \
            "$1" -o "$2" 2> "$WORK/error" || fail 'sanitized generated C'
    else
        "${CC:-cc}" -std=c11 -pthread -Wall -Wextra -Wpedantic -Werror -O3 "$1" -o "$2" 2> "$WORK/error" || fail 'generated C'
    fi
}
no_escape() { od -An -tu1 "$1" | awk '{ for(i=1;i<=NF;i++)if($i==27||$i==255||$i==155)exit 1 }' || fail 'raw control/invalid byte escaped into text'; }
"${CC:-cc}" -std=c11 -Wall -Wextra -Wpedantic -Werror "$ROOT/tests/native/json_validate.c" -o "$WORK/json"
if [ "${SANITIZE_GENERATED:-0}" = 1 ]; then
    "${CC:-cc}" -std=c11 -Wall -Wextra -Wpedantic -Werror -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all \
        "$ROOT/tests/native/diagnostic_security_045.c" "$ROOT/native/core.c" "$ROOT/native/diagnostics.c" "$ROOT/native/fast_copy.c" -o "$WORK/diagnostic"
else
    "${CC:-cc}" -std=c11 -Wall -Wextra -Wpedantic -Werror -O2 \
        "$ROOT/tests/native/diagnostic_security_045.c" "$ROOT/native/core.c" "$ROOT/native/diagnostics.c" "$ROOT/native/fast_copy.c" -o "$WORK/diagnostic"
fi
"$WORK/diagnostic" > "$WORK/output" 2> "$WORK/error" || fail 'safe spans and text'
pass 'unsafe bytes and bidi controls render visibly while UTF-8 and caret cells survive'
for deferred in no yes; do
    awk -v deferred="$deferred" 'BEGIN {
        print "type Box struct { value i64 }";
        print "func (b Box) next() Box { return Box{value: b.value + 1} }";
        print "func (b Box) report() { println(b.value) }";
        printf "func main() { var box = Box{value: 0}; ";
        if(deferred=="yes")printf "defer box";else printf "println(box";
        for(i=0;i<90;i++)printf ".next()";
        if(deferred=="yes")print ".report(); println(\"registered\") }";else print ".value) }";
    }' > "$WORK/input.kr"
    for mode in work learn; do
        timeout 10 "$KREMEN" emit-c "$WORK/input.kr" --mode="$mode" -o "$WORK/chain.c" > "$WORK/output" 2> "$WORK/error" || fail "$mode $deferred method chain exceeded bounds"
        compile "$WORK/chain.c" "$WORK/program"
        timeout 10 "$WORK/program" > "$WORK/output" 2> "$WORK/error" || fail 'method chain execution'
        if [ "$deferred" = yes ]; then printf 'registered\n90\n' > "$WORK/expected"; else printf '90\n' > "$WORK/expected"; fi
        cmp -s "$WORK/expected" "$WORK/output" || fail 'method chain result or defer ordering'
        pass "$mode traverses a 90-call $deferred-defer chain without exponential work"
    done
done
mkdir -p "$WORK/root/module" "$WORK/outside" "$WORK/header-root" "$WORK/root-neighbor"
printf 'func exposed() i64 { return 9 }\n' > "$WORK/outside/payload.kr"
ln -s ../../outside/payload.kr "$WORK/root/module/link.kr"
printf 'import "module"\nfunc main() { println(module.exposed()) }\n' > "$WORK/root/main.kr"
for mode in work learn; do
    reject "$mode rejects an escaped directory-module source" E013 check "$WORK/root/main.kr" --mode="$mode"
    grep -F '"imports"' "$WORK/error" > /dev/null || fail 'source origin'
done
"$KREMEN" check "$WORK/root/main.kr" --root "$WORK" > "$WORK/output" 2> "$WORK/error" || fail 'deliberately wider root'
pass 'an explicit wider project root admits the same source'
printf 'func main()\n' > "$WORK/outside/entry.krh"
printf 'func main() {}\n' > "$WORK/header-root/main.kr"
ln -s ../outside/entry.krh "$WORK/header-root/main.krh"
for mode in work learn; do reject "$mode rejects an escaped automatically loaded header" E013 check "$WORK/header-root/main.kr" --mode="$mode"; done
"$KREMEN" check "$WORK/header-root/main.kr" --root "$WORK" > "$WORK/output" 2> "$WORK/error" || fail 'wider header root'
pass 'headers work when their canonical target is inside the selected root'
printf 'func main() {}\n' > "$WORK/outside/main.kr"
reject 'the entry module respects an explicitly selected root' E013 check "$WORK/outside/main.kr" --root "$WORK/root"
reject 'a project root must be a directory' E010 check "$WORK/outside/main.kr" --root "$WORK/outside/main.kr"
printf 'func exposed() i64 { return 9 }\n' > "$WORK/root-neighbor/payload.kr"
rm "$WORK/root/module/link.kr"
ln -s ../../root-neighbor/payload.kr "$WORK/root/module/link.kr"
reject 'a sibling sharing the root prefix is not contained' E013 check "$WORK/root/main.kr"
rm "$WORK/root/module/link.kr"
printf 'func exposed() i64 { return 9 }\n' > "$WORK/root/real.kr"
ln -s ../real.kr "$WORK/root/module/link.kr"
"$KREMEN" check "$WORK/root/main.kr" --mode=learn > "$WORK/output" 2> "$WORK/error" || fail 'internal symlink'
pass 'internal source symlinks remain supported'

BAD_PATH=$(printf '%s/escape-\033[31m.kr' "$WORK")
printf 'func main() { var value i64 = "bad"; println(value) }\n' > "$BAD_PATH"
status=0
"$KREMEN" check "$BAD_PATH" > "$WORK/output" 2> "$WORK/error" || status=$?
[ "$status" -eq 1 ] || fail 'path diagnostic status'
no_escape "$WORK/error"
grep -F '\x1b[31m.kr:' "$WORK/error" > /dev/null || fail 'visible path escape'
pass 'text errors escape terminal commands in source names'
reject 'JSON errors preserve the escaped filename' E110 check "$BAD_PATH"
grep -F '\u001b[31m.kr' "$WORK/error" > /dev/null || fail 'JSON path bytes'
printf 'func main() { println(1) }\n' > "$BAD_PATH"
"$KREMEN" check "$BAD_PATH" > "$WORK/output" 2> "$WORK/error" || fail 'safe success status'
no_escape "$WORK/output"
grep -F '\x1b[31m.kr: OK' "$WORK/output" > /dev/null || fail 'safe status path'
pass 'successful source status escapes the same path'
printf 'func main(){println(1)}\n' > "$BAD_PATH"
status=0
"$KREMEN" fmt --check "$BAD_PATH" > "$WORK/output" 2> "$WORK/error" || status=$?
[ "$status" -eq 1 ] || fail 'format drift'
no_escape "$WORK/error"
pass 'formatter drift reports escape source names'
cat > "$WORK/input.kr" <<'KR'
import "missing\x1b[31m"
func main() {}
KR
status=0
"$KREMEN" check "$WORK/input.kr" > "$WORK/output" 2> "$WORK/error" || status=$?
[ "$status" -eq 1 ] || fail 'import path control'
no_escape "$WORK/error"
grep -F '\x1b' "$WORK/error" > /dev/null || fail 'safe import message'
pass 'untrusted import paths are escaped inside messages'
printf 'func main() { println("\377") }\n' > "$WORK/input.kr"
status=0
"$KREMEN" check "$WORK/input.kr" > "$WORK/output" 2> "$WORK/error" || status=$?
[ "$status" -eq 1 ] || fail 'invalid UTF-8 source'
no_escape "$WORK/error"
pass 'invalid source bytes are rendered as visible excerpt cells'
printf 'func main() { /* \342\200\256 */ println(missing) }\n' > "$WORK/input.kr"
status=0
"$KREMEN" check "$WORK/input.kr" > "$WORK/output" 2> "$WORK/error" || status=$?
[ "$status" -eq 1 ] || fail 'bidi source'
if grep -F "$(printf '\342\200\256')" "$WORK/error" > /dev/null; then fail 'raw bidi directive'; fi
pass 'source excerpts do not execute bidi direction directives'
awk 'BEGIN { printf "func main() { println("; for(i=0;i<18000;i++)printf "z"; print ") }" }' > "$WORK/input.kr"
status=0
"$KREMEN" check "$WORK/input.kr" > "$WORK/output" 2> "$WORK/error" || status=$?
[ "$status" -eq 1 ] || fail 'long identifier'
[ "$(wc -c < "$WORK/error")" -lt 7000 ] || fail 'unbounded text message'
grep -F '...' "$WORK/error" > /dev/null || fail 'text truncation marker'
pass 'text diagnostic messages are bounded for huge identifiers'

for kind in escape invalid; do
    if [ "$kind" = escape ]; then RUNTIME_PATH=$(printf '%s/runtime-\033[31m.kr' "$WORK"); marker='\x1b[31m.kr';
    else RUNTIME_PATH=$(printf '%s/runtime-\377.kr' "$WORK"); marker='\xff.kr'; fi
    cat > "$RUNTIME_PATH" <<'KR'
func crash(index i64) { var values = [1]i64{1}; println(values[index]) }
func main() { defer println("released"); crash(2) }
KR
    "$KREMEN" emit-c "$RUNTIME_PATH" --mode=learn -o "$WORK/runtime.c" > "$WORK/output" 2> "$WORK/error" || fail 'runtime source'
    compile "$WORK/runtime.c" "$WORK/program"
    status=0
    "$WORK/program" > "$WORK/output" 2> "$WORK/error" || status=$?
    [ "$status" -eq 1 ] || fail 'runtime status'
    no_escape "$WORK/error"
    grep -F "$marker" "$WORK/error" > /dev/null || fail 'escaped runtime path'
    grep -F 'runtime error: array index out of bounds' "$WORK/error" > /dev/null || fail 'runtime bounds check'
    grep -Fx released "$WORK/output" > /dev/null || fail 'runtime cleanup'
    pass "runtime reports safely display a $kind source path and retain cleanup"
done
printf 'Security 0.4.5 checks passed: %s.\n' "$TOTAL"
