#!/bin/sh
# Same C fixture is compiled by the native Windows PowerShell checks.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
WORK=$(mktemp -d "${TMPDIR:-/tmp}/kremen-runtime048.XXXXXX")
trap 'rm -rf "$WORK"' EXIT HUP INT TERM
COMPILER=${CC:-cc}
if [ "${SANITIZE:-0}" = 1 ]; then
    "$COMPILER" -std=c11 -pthread -Wall -Wextra -Wpedantic -Werror -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all "$ROOT/tests/native/runtime_048.c" -o "$WORK/runtime"
else
    "$COMPILER" -std=c11 -pthread -Wall -Wextra -Wpedantic -Werror -O2 "$ROOT/tests/native/runtime_048.c" -o "$WORK/runtime"
fi
TOTAL=0
for mode in unicode-file unicode-open unicode-rename unicode-same-file unicode-directory unicode-list unicode-size unicode-missing unicode-cwd invalid-path native-workers nested-workers nested-cleanup foreign-owner repeat-join invalid-worker autojoin-exit nested-failure; do
    mkdir "$WORK/$mode"
    if ! (cd "$WORK/$mode" && "$WORK/runtime" "$mode") > "$WORK/output" 2> "$WORK/error"; then
        cat "$WORK/error" >&2; printf 'FAIL: %s\n' "$mode" >&2; exit 1
    fi
    if [ "$mode" = nested-failure ]; then
        grep -F 'runtime error: native worker failure' "$WORK/error" > /dev/null || { cat "$WORK/error" >&2; exit 1; }
    else
        [ ! -s "$WORK/error" ] || { cat "$WORK/error" >&2; exit 1; }
    fi
    if grep -E 'AddressSanitizer|UndefinedBehaviorSanitizer' "$WORK/error" > /dev/null; then cat "$WORK/error" >&2; exit 1; fi
    TOTAL=$((TOTAL + 1)); printf 'ok runtime048.%s - %s\n' "$TOTAL" "$mode"
done
printf 'Runtime 0.4.8 checks passed: %s.\n' "$TOTAL"
