#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
WORK=$(mktemp -d "${TMPDIR:-/tmp}/kremen-install-tests.XXXXXX")
trap 'rm -rf "$WORK"' EXIT HUP INT TERM
PREFIX="$WORK/prefix with spaces"
mkdir -p "$PREFIX/bin" "$PREFIX/share"
printf 'preserve\n' > "$PREFIX/bin/unrelated"
printf 'preserve\n' > "$PREFIX/share/unrelated"
sh "$ROOT/install.sh" --help > "$WORK/log"
if sh "$ROOT/install.sh" --prefix relative > "$WORK/log" 2>&1; then exit 1; fi
sh "$ROOT/install.sh" --prefix "$PREFIX" > "$WORK/log" 2>&1 || { cat "$WORK/log"; exit 1; }
"$PREFIX/bin/kremen" --version | grep -F "$(cat "$ROOT/VERSION")" > /dev/null
"$PREFIX/bin/kremen" selfhost --version | grep -F 'selfhost' > /dev/null
"$PREFIX/bin/kremen-selfhost" check "$ROOT/selfhost/example.kr" > "$WORK/log"
"$PREFIX/bin/kremen" run "$ROOT/examples/hello.kr" > "$WORK/actual"
printf 'Hello, Kremen!\n' > "$WORK/expected"
cmp "$WORK/expected" "$WORK/actual"
[ -f "$PREFIX/share/doc/kremen/docs/tutorial.md" ]
[ -f "$PREFIX/share/doc/kremen/docs/threads.md" ]
[ -f "$PREFIX/share/doc/kremen/LICENSE" ]
cmp "$ROOT/LICENSE" "$PREFIX/share/doc/kremen/LICENSE"
[ -f "$PREFIX/share/doc/kremen/bench/check-results.txt" ]
[ -f "$PREFIX/share/doc/kremen/bench/runtime-comparison.txt" ]
"$PREFIX/bin/kremen" run "$PREFIX/share/doc/kremen/examples/tutorial/01_hello.kr" > "$WORK/actual"
cmp "$WORK/expected" "$WORK/actual"
# Runtime selection must prefer the installation even while the source tree exists.
printf '\n/* installed runtime lookup probe */\n' >> "$PREFIX/share/kremen/runtime/kremen_rt.h"
"$PREFIX/bin/kremen" emit-c "$ROOT/examples/hello.kr" -o "$WORK/probe.c"
grep -F 'installed runtime lookup probe' "$WORK/probe.c" > /dev/null
MOVED="$WORK/relocated prefix"
mv "$PREFIX" "$MOVED"
env -u KREMEN_SELFHOST_ROOT "$MOVED/bin/kremen" selfhost run "$ROOT/selfhost/example.kr" -- "$WORK/selfhost-example" > "$WORK/log"
printf '720|18|ok\n' > "$WORK/selfhost-expected"
cmp "$WORK/selfhost-expected" "$WORK/selfhost-example"
"$MOVED/bin/kremen" emit-c "$ROOT/examples/hello.kr" -o "$WORK/probe-moved.c"
grep -F 'installed runtime lookup probe' "$WORK/probe-moved.c" > /dev/null
"$MOVED/bin/kremen" run "$MOVED/share/doc/kremen/examples/threads.kr" > "$WORK/actual"
printf 'sum of squares: 100\n' > "$WORK/expected-threads"
cmp "$WORK/expected-threads" "$WORK/actual"
"$MOVED/bin/kremen" build "$ROOT/tests/native/v3_values.kr" -o "$WORK/app" > "$WORK/log"
"$WORK/app" > "$WORK/actual"
grep -F 'outer 1' "$WORK/actual" > /dev/null
sh "$ROOT/install.sh" --prefix "$MOVED" > "$WORK/log" 2>&1 || { cat "$WORK/log"; exit 1; }
cp "$MOVED/share/kremen/install.manifest" "$WORK/manifest"
printf '../outside\n' >> "$MOVED/share/kremen/install.manifest"
if sh "$ROOT/install.sh" --prefix "$MOVED" --uninstall > "$WORK/log" 2>&1; then exit 1; fi
[ -x "$MOVED/bin/kremen" ]
cp "$WORK/manifest" "$MOVED/share/kremen/install.manifest"
mkdir -p "$WORK/unowned/bin"
printf 'preserve\n' > "$WORK/unowned/bin/kremen"
if sh "$ROOT/install.sh" --prefix "$WORK/unowned" > "$WORK/log" 2>&1; then exit 1; fi
grep -Fx preserve "$WORK/unowned/bin/kremen" > /dev/null
sh "$ROOT/install.sh" --prefix "$MOVED" --uninstall > "$WORK/log" 2>&1 || { cat "$WORK/log"; exit 1; }
[ ! -e "$MOVED/bin/kremen" ]
[ ! -e "$MOVED/bin/kremen-selfhost" ]
[ ! -e "$MOVED/share/kremen/selfhost/runtime.c" ]
[ ! -e "$MOVED/share/kremen/runtime/kremen_rt.h" ]
grep -Fx preserve "$MOVED/bin/unrelated" > /dev/null
grep -Fx preserve "$MOVED/share/unrelated" > /dev/null
printf '%s\n' 'Installer passed: install/upgrade, spaces, runtime lookup, relocation, manifest validation, collision refusal and uninstall.'
