#!/bin/sh
# Standard library resources are independent of the user's project boundary.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
KREMEN=${1:-"$ROOT/build/kremen"}
case "$KREMEN" in /*) ;; *) KREMEN="$(pwd)/$KREMEN" ;; esac
WORK=$(mktemp -d "${TMPDIR:-/tmp}/kremen-modules049.XXXXXX")
trap 'rm -rf "$WORK"' EXIT HUP INT TERM
TOOL="$WORK/tool space"
PROJECT="$WORK/project"
LIBRARY="$TOOL/share/kremen/stdlib"
mkdir -p "$TOOL/bin" "$TOOL/share/kremen/runtime" "$LIBRARY/math" "$PROJECT/std/math" "$PROJECT/local"
cp "$KREMEN" "$TOOL/bin/kremen"
cp "$ROOT/runtime/kremen_rt.h" "$TOOL/share/kremen/runtime/"
cat > "$LIBRARY/math/math.kr" <<'KR'
package math
func value() i64 { return 42 }
KR
printf 'package math\nfunc value() i64\n' > "$LIBRARY/math/math.krh"
printf 'package math\nfunc value() i64 { return 99 }\n' > "$PROJECT/std/math/math.kr"
TOTAL=0
fail() { printf 'FAIL: %s\n' "$1" >&2; [ ! -f "$WORK/error" ] || cat "$WORK/error" >&2; exit 1; }
pass() { TOTAL=$((TOTAL + 1)); printf 'ok modules049.%s - %s\n' "$TOTAL" "$1"; }
check() {
    for profile in work learn; do
        (cd "$WORK" && "$TOOL/bin/kremen" check "$PROJECT/main.kr" --mode="$profile") > "$WORK/output" 2> "$WORK/error" || fail "$1 ($profile)"
        pass "$1 ($profile)"
    done
}
reject() {
    if (cd "$WORK" && "$TOOL/bin/kremen" check "$PROJECT/main.kr" --diagnostic-format=json) > "$WORK/output" 2> "$WORK/error"; then fail "$1: accepted"; fi
    grep -F '"code":"E013"' "$WORK/error" > /dev/null || fail "$1: wrong diagnostic"
    grep -F "$2" "$WORK/error" > /dev/null || fail "$1: wrong explanation"
    grep -F '"line":1' "$WORK/error" > /dev/null || fail "$1: import location lost"
    pass "$1"
}
printf 'import "std/math"\nfunc main() { println(math.value()) }\n' > "$PROJECT/main.kr"
check 'bundled directory import needs no wider --root'
for profile in work learn; do
    (cd "$WORK" && "$TOOL/bin/kremen" run "$PROJECT/main.kr" --mode="$profile") > "$WORK/output" 2> "$WORK/error" || fail 'run independent project'
    printf '42\n' > "$WORK/expected"
    cmp "$WORK/expected" "$WORK/output" > /dev/null || fail 'project shadowed standard module'
    pass "standard prefix cannot be shadowed by a project ($profile)"
done
printf 'import named "std/math/math.kr"\nimport "std/math"\nfunc main() { println(named.value(), math.value()) }\n' > "$PROJECT/main.kr"
check 'explicit source alias and directory import share valid declarations'
printf 'import "local"\nfunc main() { println(local.value()) }\n' > "$PROJECT/main.kr"
printf 'package local\nimport "std/math"\nfunc value() i64 { return math.value() }\n' > "$PROJECT/local/local.kr"
check 'local modules can import the standard library'

for requested in 'std/../outside' 'std/math/../../outside' 'std//math' 'std/./math' 'std/' 'std/C:math'; do
    printf 'import "%s"\nfunc main() {}\n' "$requested" > "$PROJECT/main.kr"
    reject "reject malformed path $requested" 'invalid standard library import'
done
printf 'import "std/math/math.krh"\nfunc main() {}\n' > "$PROJECT/main.kr"
reject 'headers are not import entry points' 'import the implementation .kr'
printf 'import "std/missing"\nfunc main() {}\n' > "$PROJECT/main.kr"
reject 'missing standard module identifies its import' 'cannot resolve import'
grep -F "relative to $LIBRARY" "$WORK/error" > /dev/null || fail 'missing standard module reports the actual search directory'

printf 'import "std/math"\nfunc main() { println(math.value()) }\n' > "$PROJECT/main.kr"
printf 'package outside\nfunc value() i64 { return 1 }\n' > "$WORK/outside.kr"
printf 'package math\nimport "../../../../../outside"\nfunc value() i64 { return outside.value() }\n' > "$LIBRARY/math/math.kr"
reject 'standard relative imports retain their own boundary' 'escapes standard library root'
printf 'package math\nfunc value() i64 { return 42 }\n' > "$LIBRARY/math/math.kr"
mv "$LIBRARY/math/math.krh" "$WORK/saved.krh"
ln -s "$WORK/saved.krh" "$LIBRARY/math/math.krh"
reject 'automatically paired headers cannot escape the library' 'escapes standard library root'
rm "$LIBRARY/math/math.krh"
mv "$WORK/saved.krh" "$LIBRARY/math/math.krh"
mv "$LIBRARY/math/math.kr" "$WORK/saved.kr"
ln -s "$WORK/saved.kr" "$LIBRARY/math/math.kr"
reject 'directory members cannot escape through a symlink' 'escapes standard library root'
rm "$LIBRARY/math/math.kr"
mv "$WORK/saved.kr" "$LIBRARY/math/math.kr"

printf 'import "../outside"\nfunc main() { println(outside.value()) }\n' > "$PROJECT/main.kr"
reject 'standard imports do not widen the project boundary' 'escapes project root'
mv "$LIBRARY" "$TOOL/share/kremen/saved-library"
mkdir -p "$WORK/stdlib/math"
cp "$PROJECT/std/math/math.kr" "$WORK/stdlib/math/"
printf 'import "std/math"\nfunc main() { println(math.value()) }\n' > "$PROJECT/main.kr"
reject 'missing installation cannot be replaced by a CWD library' 'standard library is unavailable'
printf 'func main() { println(7) }\n' > "$PROJECT/main.kr"
check 'programs without std imports still work without stdlib'
printf 'Module 0.4.9 checks passed: %s.\n' "$TOTAL"
