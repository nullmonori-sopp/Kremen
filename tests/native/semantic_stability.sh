#!/bin/sh
# Preserve borrow/mutability guarantees across expression and raw boundaries.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
KREMEN=${1:-"$ROOT/build/kremen"}
case "$KREMEN" in /*) ;; *) KREMEN="$(pwd)/$KREMEN" ;; esac
WORK=$(mktemp -d "${TMPDIR:-/tmp}/kremen-semantic-stability.XXXXXX")
trap 'rm -rf "$WORK"' EXIT HUP INT TERM
TOTAL=0
pass() { TOTAL=$((TOTAL + 1)); printf 'ok semantic-stability.%s - %s\n' "$TOTAL" "$1"; }
fail() { printf 'FAIL: %s\n' "$1" >&2; cat "$WORK/error" >&2; exit 1; }
reject() {
    name=$1
    cat > "$WORK/invalid.kr"
    if "$KREMEN" check "$WORK/invalid.kr" > "$WORK/log" 2> "$WORK/error"; then fail "$name was accepted"; fi
    grep -F 'E131' "$WORK/error" > /dev/null || fail "$name diagnostic"
    grep -F 'invalid.kr:' "$WORK/error" > /dev/null || fail "$name location"
    pass "$name"
}
"$KREMEN" emit-c "$ROOT/tests/native/semantic_values.kr" -o "$WORK/positive.c" > "$WORK/log" 2> "$WORK/error" || fail 'emit positive fixture'
if [ "${SANITIZE_GENERATED:-0}" = 1 ]; then
    "${CC:-cc}" -std=c11 -pthread -Wall -Wextra -Wpedantic -Werror -O1 -g \
        -fsanitize=address,undefined -fno-sanitize-recover=all "$WORK/positive.c" -o "$WORK/positive" > "$WORK/log" 2> "$WORK/error" || fail 'sanitize positive fixture'
else
    "${CC:-cc}" -std=c11 -pthread -Wall -Wextra -Wpedantic -Werror -O3 "$WORK/positive.c" -o "$WORK/positive" > "$WORK/log" 2> "$WORK/error" || fail 'compile positive fixture'
fi
"$WORK/positive" > "$WORK/actual" 2> "$WORK/error" || fail 'run positive fixture'
printf '10\n3\n8\n19\n5\n5\n22\n' > "$WORK/expected"
cmp -s "$WORK/expected" "$WORK/actual" || fail 'positive output'
pass 'catch scoped reborrow, retained outer shared loan, raw reads and mutable writes'
reject 'catch preserves an earlier mutable argument loan' <<'KR'
func result() !i64 { return ok(1) }
func pair(left &mut i64, number i64, right &mut i64) { *left += number; *right += *left }
func main() { var n = 1; pair(&mut n, result() catch 0, &mut n) }
KR
reject 'catch preserves an earlier shared argument loan' <<'KR'
func result() !i64 { return ok(1) }
func pair(left &i64, number i64, right &mut i64) { *right += *left + number }
func main() { var n = 1; pair(&n, result() catch 0, &mut n) }
KR
reject 'nested catch scopes cannot release an outer exclusive loan' <<'KR'
func result() !i64 { return fail(2) }
func pair(left &mut i64, number i64, right &mut i64) { *left += number; *right += *left }
func main() { var n = 1; pair(&mut n, result() catch _ => (result() catch 0), &mut n) }
KR
reject 'catch fallback sees an earlier exclusive argument loan' <<'KR'
func result() !i64 { return fail(2) }
func mutate(value &mut i64) i64 { *value += 1; return *value }
func pair(left &mut i64, number i64) { *left += number }
func main() { var n = 1; pair(&mut n, result() catch _ => mutate(&mut n)) }
KR
reject 'raw shared reference cannot mutate an immutable local' <<'KR'
func main() { const n i64 = 1; unsafe { var pointer = raw(&n); *pointer = 2; println(*pointer) } }
KR
reject 'raw shared reference cannot mutate a mutable local either' <<'KR'
func main() { var n = 1; unsafe { var pointer = raw(&n); *pointer = 2; println(*pointer) } }
KR
reject 'direct raw shared expression cannot bypass provenance' <<'KR'
func main() { var n = 1; unsafe { *raw(&n) = 2 }; println(n) }
KR
reject 'named shared reference preserves read-only raw provenance' <<'KR'
func main() { var n = 1; var shared = &n; unsafe { var pointer = raw(shared); *pointer = 2; println(*pointer) } }
KR
reject 'shared raw struct pointer cannot mutate a field' <<'KR'
type Box struct { number i64 }
func main() { var box = Box{number: 1}; unsafe { var pointer = raw(&box); (*pointer).number = 2; println((*pointer).number) } }
KR
reject 'shared raw array pointer cannot mutate an element' <<'KR'
func main() { var values = [2]i64{1, 2}; unsafe { var pointer = raw(&values); (*pointer)[0] = 3; println((*pointer)[0]) } }
KR
reject 'shared raw tuple pointer cannot mutate a member' <<'KR'
func main() { var values = (1, true); unsafe { var pointer = raw(&values); (*pointer).0 = 3; println((*pointer).0) } }
KR
reject 'shared raw pointer cannot enter a writable pointer function' <<'KR'
func store(pointer *i64) { unsafe { *pointer = 3 } }
func main() { var n = 1; unsafe { var pointer = raw(&n); store(pointer) } }
KR
reject 'direct shared raw expression cannot enter a writable pointer function' <<'KR'
func store(pointer *i64) { unsafe { *pointer = 3 } }
func main() { var n = 1; unsafe { store(raw(&n)) } }
KR
reject 'duplicate mutable raw pointer arguments stay exclusive' <<'KR'
func pair(left *i64, right *i64) { unsafe { *left += 1; *right += 1 } }
func main() { var n = 1; unsafe { var pointer = raw(&mut n); pair(pointer, pointer) } }
KR
printf '\n%s semantic stability checks passed.\n' "$TOTAL"
