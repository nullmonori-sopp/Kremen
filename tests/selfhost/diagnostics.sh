#!/bin/sh
# The Kremen-written frontend reports the original input, including long lines.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
DRIVER=${1:-"$ROOT/selfhost/build/kremen-selfhost"}
WORK=$(mktemp -d "${TMPDIR:-/tmp}/kremen-bootstrap-diagnostics.XXXXXX")
trap 'rm -rf "$WORK"' EXIT HUP INT TERM
TOTAL=0
reject() {
    expected=$1; status=0
    "$DRIVER" check "$WORK/input.kr" > "$WORK/output" 2> "$WORK/error" || status=$?
    [ "$status" -eq 1 ] || { cat "$WORK/error" >&2; exit 1; }
    grep -F "$expected" "$WORK/error" > /dev/null
    grep -F '^' "$WORK/error" > /dev/null
    if grep -E 'AddressSanitizer|UndefinedBehaviorSanitizer' "$WORK/error" > /dev/null; then cat "$WORK/error" >&2; exit 1; fi
    TOTAL=$((TOTAL + 1)); printf 'ok bootstrap-diagnostics.%s - %s\n' "$TOTAL" "$expected"
}
printf 'func main() i64 {\n\tvar value bool = 1\n\treturn 0\n}\n' > "$WORK/input.kr"
reject 'input.kr:2:19:'
grep -F '2 |     var value bool = 1' "$WORK/error" > /dev/null
awk 'BEGIN { print "func main() i64 {"; for(i=0;i<6000;i++)printf " "; print "return false"; print "}" }' > "$WORK/input.kr"
reject 'input.kr:2:6008:'
grep -F '...' "$WORK/error" > /dev/null
[ "$(wc -c < "$WORK/error")" -lt 1500 ]
printf 'func main() i64 {\n' > "$WORK/input.kr"
reject 'input.kr:2:1:'
grep -F '2 |' "$WORK/error" > /dev/null
printf 'func main() i64 { return @ }\n' > "$WORK/input.kr"
reject 'input.kr:1:26:'
grep -F 'return @' "$WORK/error" > /dev/null
printf 'func main() i64 { return true + 1 }\n' > "$WORK/input.kr"
reject 'input.kr:1:26:'
grep -F 'type mismatch: expected i64, found bool' "$WORK/error" > /dev/null
printf 'func main() i64 { return missing }\n' > "$WORK/input.kr"
reject 'input.kr:1:26:'
grep -F 'undefined name: missing' "$WORK/error" > /dev/null
cat > "$WORK/reopen.c" <<'C'
#include "runtime.h"
#include <stdio.h>
int main(int argc, char **argv) {
    (void)argc;
    (void)rt_read(argv[1]);
    if (remove(argv[1]) != 0) return 2;
    (void)rt_read(argv[1]);
    return 0;
}
C
if [ "${SANITIZE:-0}" = 1 ]; then
    "${CC:-cc}" -std=c11 -Wall -Wextra -Wpedantic -Werror -O1 -g \
        -fsanitize=address,undefined -fno-sanitize-recover=all -I"$ROOT/selfhost" \
        "$WORK/reopen.c" "$ROOT/selfhost/runtime.c" -o "$WORK/reopen"
else
    "${CC:-cc}" -std=c11 -Wall -Wextra -Wpedantic -Werror -O2 -I"$ROOT/selfhost" \
        "$WORK/reopen.c" "$ROOT/selfhost/runtime.c" -o "$WORK/reopen"
fi
printf 'old_source_marker\n' > "$WORK/input.kr"
status=0
"$WORK/reopen" "$WORK/input.kr" > "$WORK/output" 2> "$WORK/error" || status=$?
[ "$status" -eq 1 ]
grep -F 'input.kr:1:1: error:' "$WORK/error" > /dev/null
if grep -E 'old_source_marker|AddressSanitizer|UndefinedBehaviorSanitizer' "$WORK/error" > /dev/null; then cat "$WORK/error" >&2; exit 1; fi
TOTAL=$((TOTAL + 1)); printf 'ok bootstrap-diagnostics.%s - failed reread cannot show stale source\n' "$TOTAL"
printf 'Bootstrap diagnostics passed: %s checks.\n' "$TOTAL"
