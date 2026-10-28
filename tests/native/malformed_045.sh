#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
KREMEN=${1:-"$ROOT/build/kremen"}
case "$KREMEN" in /*) ;; *) KREMEN="$(pwd)/$KREMEN" ;; esac
WORK=$(mktemp -d "${TMPDIR:-/tmp}/kremen-malformed045.XXXXXX")
trap 'rm -rf "$WORK"' EXIT HUP INT TERM
"${CC:-cc}" -std=c11 -Wall -Wextra -Wpedantic -Werror -O2 "$ROOT/tests/native/malformed_045.c" -o "$WORK/generator"
"${CC:-cc}" -std=c11 -Wall -Wextra -Wpedantic -Werror -O2 "$ROOT/tests/native/json_validate.c" -o "$WORK/json"
"$WORK/generator" "$WORK"
TOTAL=0
for file in "$WORK"/case-*.kr; do
    for mode in work learn; do
        status=0
        timeout 10 "$KREMEN" emit-c "$file" --mode="$mode" --diagnostic-format=json -o "$WORK/output.c" > "$WORK/output" 2> "$WORK/error" || status=$?
        if [ "$status" -eq 0 ]; then
            [ ! -s "$WORK/error" ] || { cat "$WORK/error" >&2; exit 1; }
            timeout 10 "${CC:-cc}" -std=c11 -pthread -Wall -Wextra -Wpedantic -Werror -fsyntax-only "$WORK/output.c" > "$WORK/output" 2> "$WORK/error" || {
                printf 'FAIL: accepted mutation produces invalid C: %s %s\n' "$(basename "$file")" "$mode" >&2
                cat "$WORK/error" >&2; od -An -tx1 "$file" >&2; exit 1;
            }
        elif [ "$status" -eq 1 ]; then
            "$WORK/json" "$WORK/error" || { od -An -tx1 "$WORK/error" >&2; exit 1; }
            grep -F "\"mode\":\"$mode\"" "$WORK/error" > /dev/null || exit 1
        else
            printf 'FAIL: mutation %s in %s exited %s\n' "$(basename "$file")" "$mode" "$status" >&2
            od -An -tx1 "$file" >&2; cat "$WORK/error" >&2; exit 1
        fi
        TOTAL=$((TOTAL + 1))
        printf 'ok malformed045.%s - %s in %s is bounded and returns valid C or JSON\n' "$TOTAL" "$(basename "$file")" "$mode"
    done
done
printf 'Deterministic mutation checks passed: %s (128 sources, 2 profiles; seed 0x4b524d45).\n' "$TOTAL"
