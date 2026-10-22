#!/bin/sh
# Measure only bootstrap translation; building the benchmark's timer is untimed.
set -eu
LC_ALL=C
export LC_ALL
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
COMPILER="$ROOT/selfhost/build/stage2"
SOURCE="$ROOT/selfhost/bootstrap.kr"
RUNS=10
usage() { printf '%s\n' 'Usage: sh bench/bootstrap.sh [--compiler PATH] [--source FILE.kr] [--runs N]'; }
while [ "$#" -gt 0 ]; do
    case "$1" in
        --compiler|--source|--runs)
            [ "$#" -ge 2 ] || { usage >&2; exit 2; }
            case "$1" in --compiler) COMPILER=$2 ;; --source) SOURCE=$2 ;; --runs) RUNS=$2 ;; esac
            shift 2 ;;
        --help|-h) usage; exit 0 ;;
        *) usage >&2; exit 2 ;;
    esac
done
if ! awk -v count="$RUNS" 'BEGIN { exit !(count ~ /^[0-9]+$/ && count+0 >= 1 && count+0 <= 100000) }'; then usage >&2; exit 2; fi
case "$COMPILER" in /*) ;; *) COMPILER="$(pwd)/$COMPILER" ;; esac
case "$SOURCE" in /*) ;; *) SOURCE="$(pwd)/$SOURCE" ;; esac
[ -x "$COMPILER" ] || { printf 'Missing bootstrap compiler: %s\nRun make selfhost first.\n' "$COMPILER" >&2; exit 1; }
[ -f "$SOURCE" ] || { printf 'Missing source: %s\n' "$SOURCE" >&2; exit 1; }
WORK=$(mktemp -d "${TMPDIR:-/tmp}/kremen-bootstrap-bench.XXXXXX")
trap 'rm -rf "$WORK"' EXIT HUP INT TERM
"${CC:-cc}" -std=c11 -Wall -Wextra -Wpedantic -Werror -O2 "$ROOT/bench/time.c" -o "$WORK/timer"
"$COMPILER" "$SOURCE" "$WORK/program.c"
cp "$WORK/program.c" "$WORK/expected.c"
printf '%s\n' 'Kremen bootstrap translation benchmark (startup and atomic C output included)'
printf 'host: %s\n' "$(uname -sm)"
printf 'compiler POSIX cksum: %s\n' "$(cksum < "$COMPILER")"
printf 'source bytes / POSIX cksum: %s\n' "$(cksum < "$SOURCE")"
printf 'runs: %s; warmup: 1; operation: lexer/parser/typecheck/C generation/file output\n' "$RUNS"
if ! "$WORK/timer" "$RUNS" "$COMPILER" "$SOURCE" "$WORK/program.c" > /dev/null 2> "$WORK/time"; then cat "$WORK/time" >&2; exit 1; fi
cmp "$WORK/expected.c" "$WORK/program.c"
cat "$WORK/time"
printf 'generated C POSIX cksum: %s\n' "$(cksum < "$WORK/program.c")"
printf '%s\n' 'The batch mean is host/workload specific; C compilation and generated-program speed are not measured.'
