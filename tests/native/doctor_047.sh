#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
KREMEN=${1:-"$ROOT/build/kremen"}
case "$KREMEN" in /*) ;; *) KREMEN="$(pwd)/$KREMEN" ;; esac
WORK=$(mktemp -d "${TMPDIR:-/tmp}/kremen-doctor047.XXXXXX")
trap 'rm -rf "$WORK"' EXIT HUP INT TERM
TOTAL=0
pass() { TOTAL=$((TOTAL + 1)); printf 'ok doctor047.%s - %s\n' "$TOTAL" "$1"; }
fail() { printf 'FAIL: %s\n' "$1" >&2; cat "$WORK/error" >&2; exit 1; }
"${CC:-cc}" -std=c11 -Wall -Wextra -Wpedantic -Werror "$ROOT/tests/native/json_validate.c" -o "$WORK/json"
"$KREMEN" doctor > "$WORK/output" 2> "$WORK/error" || fail 'host probe'
grep -Fx 'Worker spawn/join: OK' "$WORK/output" > /dev/null || fail 'worker probe'
[ ! -s "$WORK/error" ] || fail 'unexpected doctor error'
pass 'doctor compiles and runs the actual C11 pthread runtime'
KREMEN_DOCTOR_REAL_CC=$(command -v "${CC:-cc}")
export KREMEN_DOCTOR_REAL_CC
cat > "$WORK/compiler with spaces" <<'SH'
#!/bin/sh
exec "$KREMEN_DOCTOR_REAL_CC" "$@"
SH
chmod +x "$WORK/compiler with spaces"
"$KREMEN" doctor --cc "$WORK/compiler with spaces" > "$WORK/output" 2> "$WORK/error" || fail 'explicit compiler'
pass 'doctor passes a compiler path with spaces as one argument'
reject() {
    label=$1; code=$2; shift 2
    status=0
    "$KREMEN" doctor "$@" --diagnostic-format=json > "$WORK/output" 2> "$WORK/error" || status=$?
    [ "$status" -eq 1 ] || fail "$label status=$status"
    [ ! -s "$WORK/output" ] || fail "$label stdout"
    "$WORK/json" "$WORK/error" || fail "$label JSON"
    grep -F "\"code\":\"$code\"" "$WORK/error" > /dev/null || fail "$label code"
    pass "$label"
}
reject 'missing backend has an explained structured failure' E016 --cc "$WORK/missing"
grep -F 'kremen explain E016' "$WORK/error" > /dev/null || fail 'offline explanation link'
reject 'a failing backend is rejected' E016 --cc /bin/false
reject 'compiler names are never interpreted by a shell' E016 --cc "missing;touch $WORK/injected"
[ ! -e "$WORK/injected" ] || fail 'shell injection'
reject 'doctor accepts no source path' E001 main.kr
reject 'doctor accepts no checking profile' E001 --mode=learn
reject 'doctor accepts no project root' E001 --root "$WORK"
reject 'doctor accepts no optimization option' E001 -O0
reject 'doctor accepts no output artifact' E001 -o "$WORK/output-file"
reject 'doctor accepts no catalog option' E001 --list
reject 'doctor accepts no formatter option' E001 --check
CC=/missing "$KREMEN" explain E016 > "$WORK/output" 2> "$WORK/error" || fail 'offline explanation'
grep -F 'Toolchain self-check failed' "$WORK/output" > /dev/null || fail 'explanation title'
pass 'doctor explanation works without the backend'
mkdir -p "$WORK/prefix/bin" "$WORK/prefix/share/kremen/runtime"
cp "$KREMEN" "$WORK/prefix/bin/kremen"
cp "$ROOT/runtime/kremen_rt.h" "$WORK/prefix/share/kremen/runtime/kremen_rt.h"
"$WORK/prefix/bin/kremen" doctor > "$WORK/output" 2> "$WORK/error" || fail 'relocated probe'
grep -F "$WORK/prefix/share/kremen/runtime/kremen_rt.h" "$WORK/output" > /dev/null || fail 'installed runtime'
pass 'doctor uses the runtime beside a relocated compiler'
printf 'Doctor 0.4.7 checks passed: %s.\n' "$TOTAL"
