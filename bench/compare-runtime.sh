#!/bin/sh
# Compare only runtime implementation using the SAME released frontend and input.
set -eu
[ "$#" -eq 1 ] || { printf '%s\n' 'Usage: sh bench/compare-runtime.sh /path/to/kremen-v0.4.0.zip' >&2; exit 2; }
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
ARCHIVE=$1
case "$ARCHIVE" in /*) ;; *) ARCHIVE="$(pwd)/$ARCHIVE" ;; esac
WORK=$(mktemp -d "${TMPDIR:-/tmp}/kremen-runtime-compare.XXXXXX")
trap 'rm -rf "$WORK"' EXIT HUP INT TERM
mkdir "$WORK/old"
for file in stage0.c runtime.c runtime.h bootstrap.kr driver.c; do
    unzip -p "$ARCHIVE" "kremen/selfhost/$file" > "$WORK/old/$file"
done
CC_BIN=${CC:-cc}
"$CC_BIN" -std=c11 -Wall -Wextra -Wpedantic -Werror -O2 "$ROOT/bench/time.c" -o "$WORK/timer"
"$CC_BIN" -std=c11 -Wall -Wextra -Wpedantic -Werror -O2 "$WORK/old/stage0.c" "$WORK/old/runtime.c" "$WORK/old/driver.c" -o "$WORK/old-runtime"
"$CC_BIN" -std=c11 -Wall -Wextra -Wpedantic -Werror -O2 "$WORK/old/stage0.c" "$ROOT/selfhost/runtime.c" "$WORK/old/driver.c" -o "$WORK/new-runtime"
"$WORK/old-runtime" "$WORK/old/bootstrap.kr" "$WORK/old.c"
"$WORK/new-runtime" "$WORK/old/bootstrap.kr" "$WORK/new.c"
cmp "$WORK/old.c" "$WORK/new.c"
printf '%s\n' 'Bootstrap runtime comparison: unchanged0.4.0 frontend/source, strict C11-O2'
printf 'host: %s; input cksum: %s\n' "$(uname -sm)" "$(cksum < "$WORK/old/bootstrap.kr")"
printf '%s\n' 'method: 3 alternating pairs, 5 processes/batch after warmup; wall/CPU/RSS'
for round in 1 2 3; do
    for runtime in old new; do
        printf 'round %s runtime %s\n' "$round" "$runtime"
        "$WORK/timer" 5 "$WORK/$runtime-runtime" "$WORK/old/bootstrap.kr" "$WORK/$runtime.c" > /dev/null
    done
done
cmp "$WORK/old.c" "$WORK/new.c"
printf '%s\n' 'Generated C is identical. This isolates runtime, not total0.4.1 safety/codegen cost.'
