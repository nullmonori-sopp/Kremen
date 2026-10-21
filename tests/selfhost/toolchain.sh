#!/bin/sh
# Integration tests for the Kremen-written self-hosting driver. No Python.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
DRIVER=${1:-"$ROOT/selfhost/build/kremen-selfhost"}
case "$DRIVER" in /*) ;; *) DRIVER="$(pwd)/$DRIVER" ;; esac
[ -x "$DRIVER" ] || { printf 'Missing selfhost driver: %s\nRun make selfhost first.\n' "$DRIVER" >&2; exit 1; }
WORK=$(mktemp -d "${TMPDIR:-/tmp}/kremen-selfhost-tests.XXXXXX")
trap 'rm -rf "$WORK"' EXIT HUP INT TERM
TOTAL=0
pass() { TOTAL=$((TOTAL + 1)); printf 'ok selfhost.%s - %s\n' "$TOTAL" "$1"; }
fail() {
    printf 'FAIL: %s\n' "$1" >&2
    [ ! -s "$WORK/error" ] || cat "$WORK/error" >&2
    exit 1
}
run_ok() {
    "$@" > "$WORK/output" 2> "$WORK/error" || fail "command failed: $*"
}
reject() {
    label=$1; shift
    if "$@" > "$WORK/output" 2> "$WORK/error"; then fail "$label accepted"; fi
    [ -s "$WORK/error" ] || fail "$label has no diagnostic"
    pass "$label"
}
expect_status() {
    label=$1; expected_status=$2; shift 2
    actual_status=0
    "$@" > "$WORK/output" 2> "$WORK/error" || actual_status=$?
    [ "$actual_status" -eq "$expected_status" ] || fail "$label: expected exit $expected_status, got $actual_status"
    pass "$label"
}

mkdir "$WORK/path with spaces ; literal"
SOURCE="$WORK/path with spaces ; literal/source \$(literal).kr"
cat > "$SOURCE" <<'KR'
extern func rt_arg(index i64) string
extern func rt_cat(left string, right string) string
extern func rt_write(path string, value string) i64
func main() i64 {
    return rt_write(rt_arg(1), rt_cat(rt_arg(2), rt_cat("|", rt_arg(3))))
}
KR
cp "$SOURCE" "$WORK/original.kr"

run_ok "$DRIVER" --help
grep -F 'bootstrap' "$WORK/output" > /dev/null || fail 'help missing bootstrap command'
run_ok "$DRIVER" --version
grep -i -F 'selfhost' "$WORK/output" > /dev/null || fail 'version missing selfhost identity'
pass 'help and driver version'

KREMEN_TEST_REAL_CC=${CC:-cc}
case "$KREMEN_TEST_REAL_CC" in /*) ;; *) KREMEN_TEST_REAL_CC=$(command -v "$KREMEN_TEST_REAL_CC") ;; esac
KREMEN_TEST_CC_LOG="$WORK/compiler.arguments"
export KREMEN_TEST_REAL_CC KREMEN_TEST_CC_LOG
COMPILER="$WORK/compiler with spaces"
cat > "$COMPILER" <<'CC'
#!/bin/sh
printf '%s\n' "$@" >> "$KREMEN_TEST_CC_LOG"
exec "$KREMEN_TEST_REAL_CC" "$@"
CC
chmod 755 "$COMPILER"
run_ok "$DRIVER" check "$SOURCE" --cc "$COMPILER"
for flag in -std=c11 -Wall -Wextra -Wpedantic -Werror -fsyntax-only; do
    grep -Fx -- "$flag" "$KREMEN_TEST_CC_LOG" > /dev/null || fail "check did not use $flag"
done
pass 'check invokes strict C backend and handles literal source paths'

run_ok "$DRIVER" emit-c "$SOURCE" -o "$WORK/emitted source.c"
[ -s "$WORK/emitted source.c" ] || fail 'empty emitted C'
grep -F 'kr_main' "$WORK/emitted source.c" > /dev/null || fail 'emitted C has no entry point'
pass 'emit-c produces a complete C translation unit'

BINARY="$WORK/path with spaces ; literal/output \$(literal)"
run_ok "$DRIVER" build "$SOURCE" --cc "$COMPILER" -O0 -o "$BINARY"
grep -Fx -- -O0 "$KREMEN_TEST_CC_LOG" > /dev/null || fail 'explicit optimization was not passed'
run_ok "$BINARY" "$WORK/built.args" 'first arg' 'second arg'
printf '%s' 'first arg|second arg' > "$WORK/expected"
cmp -s "$WORK/expected" "$WORK/built.args" || fail 'standalone binary arguments changed'
pass 'build links runtime and handles source/output/compiler paths with spaces'

ARGUMENT_ONE='$(touch injected-one) ; | & < > * ? [x]'
ARGUMENT_TWO='; touch injected-two'
(cd "$WORK" && "$DRIVER" run "$SOURCE" --cc "$COMPILER" -- "$WORK/run.args" "$ARGUMENT_ONE" "$ARGUMENT_TWO") > "$WORK/output" 2> "$WORK/error" || fail 'run with literal arguments'
printf '%s|%s' "$ARGUMENT_ONE" "$ARGUMENT_TWO" > "$WORK/expected"
cmp -s "$WORK/expected" "$WORK/run.args" || fail 'run changed literal arguments'
[ ! -e "$WORK/injected-one" ] && [ ! -e "$WORK/injected-two" ] || fail 'shell interpreted program arguments'
grep -Fx -- -O3 "$KREMEN_TEST_CC_LOG" > /dev/null || fail 'default compilation did not use -O3'
pass 'run forwards spaces and shell metacharacters as literal argv'

cat > "$WORK/status.kr" <<'KR'
func main() i64 { return 7 }
KR
expect_status 'run preserves child exit code' 7 "$DRIVER" run "$WORK/status.kr"
cat > "$WORK/killed compiler" <<'CC'
#!/bin/sh
kill -TERM "$$"
CC
chmod 755 "$WORK/killed compiler"
expect_status 'backend signal status is 128 plus signal' 143 "$DRIVER" check "$SOURCE" --cc "$WORK/killed compiler"
expect_status 'missing compiler preserves exec failure status' 127 "$DRIVER" check "$SOURCE" --cc "$WORK/no-such-compiler"

cat > "$WORK/invalid.kr" <<'KR'
func main() i64 { var wrong i64 = true; return wrong }
KR
reject 'frontend rejects implicit bool-to-integer conversion' "$DRIVER" check "$WORK/invalid.kr"
grep -F 'invalid.kr:' "$WORK/error" > /dev/null || fail 'semantic error lost source location'
printf 'previous artifact\n' > "$WORK/existing artifact"
cp "$WORK/existing artifact" "$WORK/expected"
reject 'frontend failure does not replace an existing binary' "$DRIVER" build "$WORK/invalid.kr" -o "$WORK/existing artifact"
cmp -s "$WORK/expected" "$WORK/existing artifact" || fail 'frontend failure destroyed binary'
reject 'frontend failure does not replace emitted C' "$DRIVER" emit-c "$WORK/invalid.kr" -o "$WORK/existing artifact"
cmp -s "$WORK/expected" "$WORK/existing artifact" || fail 'frontend failure destroyed emitted C'
reject 'backend failure does not replace an existing binary' "$DRIVER" build "$SOURCE" --cc "$WORK/no-such-compiler" -o "$WORK/existing artifact"
cmp -s "$WORK/expected" "$WORK/existing artifact" || fail 'backend failure destroyed binary'

ln "$SOURCE" "$WORK/hardlink.kr"
ln -s "$SOURCE" "$WORK/symlink.kr"
for target in "$SOURCE" "$WORK/hardlink.kr" "$WORK/symlink.kr"; do
    reject 'emit-c refuses source aliases' "$DRIVER" emit-c "$SOURCE" -o "$target"
    cmp -s "$WORK/original.kr" "$SOURCE" || fail 'source overwritten through an alias'
done
reject 'build refuses source alias' "$DRIVER" build "$SOURCE" -o "$WORK/hardlink.kr"
cmp -s "$WORK/original.kr" "$SOURCE" || fail 'build overwrote source'
# Exercise these guards against disposable copies, so a regression cannot damage
# either the checkout or the driver used by the rest of this test run.
mkdir "$WORK/resources"
for resource in bootstrap.kr toolchain.kr runtime.c runtime.h driver.c host.c host.h; do
    cp "$ROOT/selfhost/$resource" "$WORK/resources/$resource"
done
cp "$WORK/resources/runtime.c" "$WORK/runtime.original.c"
reject 'output cannot replace a bundled runtime source' env KREMEN_SELFHOST_ROOT="$WORK/resources" "$DRIVER" emit-c "$SOURCE" -o "$WORK/resources/runtime.c"
cmp -s "$WORK/runtime.original.c" "$WORK/resources/runtime.c" || fail 'runtime source was overwritten'
cp "$DRIVER" "$WORK/protected driver"
cp "$WORK/protected driver" "$WORK/driver.original"
reject 'output cannot replace the running selfhost driver' env KREMEN_SELFHOST_ROOT="$WORK/resources" "$WORK/protected driver" build "$SOURCE" -o "$WORK/protected driver"
cmp -s "$WORK/driver.original" "$WORK/protected driver" || fail 'running driver was overwritten'

reject 'unknown command' "$DRIVER" unknown "$SOURCE"
reject 'unknown option' "$DRIVER" check "$SOURCE" --unexpected-option
reject 'missing source' "$DRIVER" check
reject 'missing output argument' "$DRIVER" emit-c "$SOURCE" -o
reject 'emit-c requires an output file' "$DRIVER" emit-c "$SOURCE"
reject 'missing compiler argument' "$DRIVER" build "$SOURCE" --cc
reject 'extra source argument' "$DRIVER" check "$SOURCE" "$SOURCE"

run_ok "$DRIVER" bootstrap --cc "$COMPILER" -o "$WORK/rebuilt driver"
grep -i -E 'fixed.point|verified' "$WORK/output" > /dev/null || fail 'bootstrap did not report source fixed point'
[ -x "$WORK/rebuilt driver" ] || fail 'bootstrap did not publish an executable'
KREMEN_SELFHOST_ROOT="$ROOT/selfhost" "$WORK/rebuilt driver" check "$SOURCE" > "$WORK/output" 2> "$WORK/error" || fail 'rebuilt driver cannot check source'
KREMEN_SELFHOST_ROOT="$ROOT/selfhost" "$WORK/rebuilt driver" run "$WORK/status.kr" > "$WORK/output" 2> "$WORK/error" && rebuilt_status=0 || rebuilt_status=$?
[ "$rebuilt_status" -eq 7 ] || fail 'rebuilt driver lost run status'
pass 'bootstrap rebuilds the compiler and driver and verifies source fixed point'

printf '\n%s selfhost driver integration checks passed.\n' "$TOTAL"
