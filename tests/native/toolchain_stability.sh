#!/bin/sh
# Portability and artifact/CLI safety. Source and output probes use disposable copies.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
KREMEN=${1:-"$ROOT/build/kremen"}
case "$KREMEN" in /*) ;; *) KREMEN="$(pwd)/$KREMEN" ;; esac
WORK=$(mktemp -d "${TMPDIR:-/tmp}/kremen-toolchain-stability.XXXXXX")
trap 'rm -rf "$WORK"' EXIT HUP INT TERM
TOTAL=0
pass() { TOTAL=$((TOTAL + 1)); printf 'ok toolchain-stability.%s - %s\n' "$TOTAL" "$1"; }
fail() { printf 'FAIL: %s\n' "$1" >&2; [ ! -s "$WORK/error" ] || cat "$WORK/error" >&2; exit 1; }
reject() {
    name=$1; shift
    status=0
    "$@" > "$WORK/log" 2> "$WORK/error" || status=$?
    [ "$status" -eq 1 ] || fail "$name: expected diagnostic status1, got $status"
    [ -s "$WORK/error" ] || fail "$name missing diagnostic"
    pass "$name"
}
COMPILER=${CC:-cc}
if [ "${SANITIZE:-0}" = 1 ]; then
    "$COMPILER" -std=c11 -pthread -Wall -Wextra -Wpedantic -Werror -O1 -g -DKM_NO_PROC_SELF_EXE=1 \
        -fsanitize=address,undefined -fno-sanitize-recover=all \
        "$ROOT/tests/native/platform_stability.c" "$ROOT/native/core.c" "$ROOT/native/fast_copy.c" \
        "$ROOT/selfhost/host.c" "$ROOT/selfhost/runtime.c" -o "$WORK/probe"
else
    "$COMPILER" -std=c11 -pthread -Wall -Wextra -Wpedantic -Werror -O2 -DKM_NO_PROC_SELF_EXE=1 \
        "$ROOT/tests/native/platform_stability.c" "$ROOT/native/core.c" "$ROOT/native/fast_copy.c" \
        "$ROOT/selfhost/host.c" "$ROOT/selfhost/runtime.c" -o "$WORK/probe"
fi
mkdir "$WORK/path with spaces"
ln -s "$WORK/probe" "$WORK/path with spaces/platform-probe"
PATH="$WORK/path with spaces:$PATH" platform-probe > "$WORK/actual"
printf '%s\n' "$WORK/probe" > "$WORK/expected"
cmp "$WORK/expected" "$WORK/actual"
pass 'no-proc discovery follows PATH and symlinks with spaces'
(cd "$WORK" && PATH=":$PATH" probe) > "$WORK/actual"
cmp "$WORK/expected" "$WORK/actual"
(cd "$WORK" && ./probe) > "$WORK/actual"
cmp "$WORK/expected" "$WORK/actual"
pass 'no-proc discovery supports empty PATH entry and relative executable'
"$WORK/probe" limits > "$WORK/log" 2> "$WORK/error" || fail 'allocation limits'
grep -F '512 MiB' "$WORK/error" > /dev/null || fail 'arena budget diagnostic'
pass 'arena/string/vector overflow diagnostics occur before allocation; cleanup is idempotent'
"$WORK/probe" argv > "$WORK/log" 2> "$WORK/error" || fail 'dynamic arguments'
pass 'process argv grows beyond255 arguments and preserves bytes/status'
printf 'func main() { println(41) }\n' > "$WORK/-source.kr"
(cd "$WORK" && "$KREMEN" check -- -source.kr && "$KREMEN" fmt -w -- -source.kr) > "$WORK/log" 2> "$WORK/error" || fail 'end-of-options handling'
pass 'native source paths beginning with dash work after --'
(cd "$WORK" && "$KREMEN" build -o -program -- -source.kr && ./-program) > "$WORK/actual" 2> "$WORK/error" || fail 'dash-leading build output'
tail -1 "$WORK/actual" > "$WORK/last-line"
printf '41\n' > "$WORK/expected"
cmp "$WORK/expected" "$WORK/last-line"
pass 'dash-leading output is passed to the C backend through an absolute staging path'
printf 'previous artifact\n' > "$WORK/existing"
reject 'check rejects ignored output option' "$KREMEN" check "$WORK/-source.kr" -o "$WORK/existing"
reject 'run rejects ignored output option' "$KREMEN" run "$WORK/-source.kr" -o "$WORK/existing"
reject 'formatter rejects ignored optimization option' "$KREMEN" fmt -O3 "$WORK/-source.kr"
reject 'formatter rejects ignored output option' "$KREMEN" fmt "$WORK/-source.kr" -o "$WORK/existing"
reject 'empty C compiler is diagnosed' "$KREMEN" build "$WORK/-source.kr" --cc ''
grep -Fx 'previous artifact' "$WORK/existing" > /dev/null || fail 'CLI errors replaced output'
ln "$WORK/-source.kr" "$WORK/hardlink.kr"
ln -s "$WORK/-source.kr" "$WORK/symlink.kr"
reject 'emission protects source hardlink' "$KREMEN" emit-c "$WORK/-source.kr" -o "$WORK/hardlink.kr"
reject 'emission protects source symlink' "$KREMEN" emit-c "$WORK/-source.kr" -o "$WORK/symlink.kr"
mkdir -p "$WORK/prefix/bin" "$WORK/prefix/share/kremen/runtime"
cp "$KREMEN" "$WORK/prefix/bin/kremen"
cp "$ROOT/runtime/kremen_rt.h" "$WORK/prefix/share/kremen/runtime/kremen_rt.h"
cp "$WORK/prefix/bin/kremen" "$WORK/compiler.original"
cp "$WORK/prefix/share/kremen/runtime/kremen_rt.h" "$WORK/runtime.original"
reject 'emission protects executing compiler' "$WORK/prefix/bin/kremen" emit-c "$WORK/-source.kr" -o "$WORK/prefix/bin/kremen"
reject 'emission protects selected runtime' "$WORK/prefix/bin/kremen" emit-c "$WORK/-source.kr" -o "$WORK/prefix/share/kremen/runtime/kremen_rt.h"
cmp "$WORK/compiler.original" "$WORK/prefix/bin/kremen"
cmp "$WORK/runtime.original" "$WORK/prefix/share/kremen/runtime/kremen_rt.h"
# A clean C-only build checks Makefile quoting and complete operation without /proc.
mkdir "$WORK/source path's quote"
tar -C "$ROOT" -cf "$WORK/source.tar" Makefile VERSION native runtime selfhost/platform.h
tar -C "$WORK/source path's quote" -xf "$WORK/source.tar"
KREMEN_TEST_REAL_CC=$(command -v "$COMPILER")
export KREMEN_TEST_REAL_CC
cat > "$WORK/compiler ' path" <<'CC'
#!/bin/sh
exec "$KREMEN_TEST_REAL_CC" -DKM_NO_PROC_SELF_EXE=1 "$@"
CC
chmod 755 "$WORK/compiler ' path"
make -C "$WORK/source path's quote" CC="$WORK/compiler ' path" USE_ASM=0 CFLAGS=-O1 all > "$WORK/log" 2> "$WORK/error" || fail 'quoted C-only make'
"$WORK/source path's quote/build/kremen" run "$WORK/-source.kr" > "$WORK/actual" 2> "$WORK/error" || fail 'no-proc native execution'
printf '41\n' > "$WORK/expected"
cmp "$WORK/expected" "$WORK/actual"
pass 'clean C-only compiler builds and runs without /proc from quoted source/compiler paths'
printf '\n%s toolchain stability checks passed.\n' "$TOTAL"
