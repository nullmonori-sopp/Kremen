#!/bin/sh
# Host-neutral runtime regressions, also runnable with MSYS2 UCRT64 GCC.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
WORK=$(mktemp -d "${TMPDIR:-/tmp}/kremen-runtime047.XXXXXX")
trap 'rm -rf "$WORK"' EXIT HUP INT TERM
COMPILER=${CC:-cc}
if [ "${SANITIZE:-0}" = 1 ]; then
    "$COMPILER" -std=c11 -pthread -Wall -Wextra -Wpedantic -Werror -O1 -g \
        -fsanitize=address,undefined -fno-sanitize-recover=all \
        "$ROOT/tests/native/runtime_047.c" -o "$WORK/runtime"
else
    "$COMPILER" -std=c11 -pthread -Wall -Wextra -Wpedantic -Werror -O2 \
        "$ROOT/tests/native/runtime_047.c" -o "$WORK/runtime"
fi
TOTAL=0
for mode in sticky-write modes-short modes-binary write-append-bytes rename-replace rename-same rename-missing directory-lifecycle directory-nonempty sorted-list invalid-path-mode thread-join; do
    mkdir "$WORK/$mode"
    if ! (cd "$WORK/$mode" && "$WORK/runtime" "$mode") > "$WORK/output" 2> "$WORK/error"; then
        cat "$WORK/error" >&2; printf 'FAIL: %s\n' "$mode" >&2; exit 1
    fi
    [ ! -s "$WORK/error" ] || { cat "$WORK/error" >&2; exit 1; }
    TOTAL=$((TOTAL + 1)); printf 'ok runtime047.%s - %s\n' "$TOTAL" "$mode"
done
printf 'Runtime 0.4.7 checks passed: %s.\n' "$TOTAL"
