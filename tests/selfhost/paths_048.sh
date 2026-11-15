#!/bin/sh
# A generated program's embedded source name must be stable across host path
# separators. Backslashes are legal POSIX filename bytes, so they also let this
# regression exercise Windows-style input spellings on a Linux build.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
TRANSLATOR=${1:-"$ROOT/selfhost/build/stage2"}
case "$TRANSLATOR" in /*) ;; *) TRANSLATOR="$(pwd)/$TRANSLATOR" ;; esac
WORK=$(mktemp -d "${TMPDIR:-/tmp}/kremen-subset-path048.XXXXXX")
trap 'rm -rf "$WORK"' EXIT HUP INT TERM
TOTAL=0
fail() { printf 'FAIL: %s\n' "$1" >&2; cat "$WORK/error" >&2; exit 1; }
mkdir "$WORK/slash" "$WORK/mixed\\component"
for spelling in 'slash/probe.kr' 'windows\probe.kr' 'mixed\component/probe.kr' 'C:\source\probe.kr'; do
    source="$WORK/$spelling"
    cat > "$source" <<'KR'
func main() i64 {
    var value i64 = 9223372036854775807
    var increment i64 = 1
    return value + increment
}
KR
    "$TRANSLATOR" "$source" "$WORK/program.c" > "$WORK/output" 2> "$WORK/error" || fail "$spelling translation"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Wpedantic -Werror -O1 \
        "$WORK/program.c" "$ROOT/selfhost/runtime.c" "$ROOT/selfhost/driver.c" -o "$WORK/program" 2> "$WORK/error" || fail "$spelling compilation"
    status=0
    "$WORK/program" > "$WORK/output" 2> "$WORK/error" || status=$?
    [ "$status" -eq 1 ] || fail "$spelling runtime status=$status"
    [ ! -s "$WORK/output" ] || fail "$spelling stdout"
    grep -E '^probe\.kr:[0-9]+:[0-9]+: error: ' "$WORK/error" > /dev/null || fail "$spelling source basename"
    grep -F 'overflow' "$WORK/error" > /dev/null || fail "$spelling checked arithmetic"
    TOTAL=$((TOTAL + 1)); printf 'ok subset-path048.%s - %s retains the source basename\n' "$TOTAL" "$spelling"
done
printf 'Subset path 0.4.8 checks passed: %s.\n' "$TOTAL"
