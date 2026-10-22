#!/bin/sh
# Deterministic compiler-throughput benchmark using native tools only.
set -eu
LC_ALL=C
export LC_ALL

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
COMPILER="$ROOT/build/kremen"
RUNS=20
FUNCTIONS=2000

usage() {
    printf '%s\n' 'Usage: sh bench/check.sh [--compiler PATH] [--runs N] [--functions N]'
    printf '%s\n' 'Measures complete check processes, including startup; defaults: 2000 functions, 20 runs.'
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --compiler|--runs|--functions)
            if [ "$#" -lt 2 ]; then usage >&2; exit 2; fi
            case "$1" in
                --compiler) COMPILER=$2 ;;
                --runs) RUNS=$2 ;;
                --functions) FUNCTIONS=$2 ;;
            esac
            shift 2 ;;
        --help|-h) usage; exit 0 ;;
        *) usage >&2; exit 2 ;;
    esac
done
for count in "$RUNS" "$FUNCTIONS"; do
    if ! awk -v count="$count" 'BEGIN { exit !(count ~ /^[0-9]+$/ && count + 0 >= 1 && count + 0 <= 100000) }'; then
        printf 'Counts must be integers between 1 and 100000: %s\n' "$count" >&2
        exit 2
    fi
done
case "$COMPILER" in
    /*) ;;
    *) COMPILER="$(pwd)/$COMPILER" ;;
esac
if [ ! -x "$COMPILER" ]; then
    printf 'Native compiler missing: %s\nRun make first.\n' "$COMPILER" >&2
    exit 1
fi
WORK=$(mktemp -d "${TMPDIR:-/tmp}/kremen-check-bench.XXXXXX")
trap 'rm -rf "$WORK"' EXIT HUP INT TERM
TIMER="$WORK/time"
"${CC:-cc}" -std=c11 -Wall -Wextra -Wpedantic -Werror -O2 "$ROOT/bench/time.c" -o "$TIMER"
SOURCE="$WORK/generated.kr"
awk -v functions="$FUNCTIONS" 'BEGIN {
    print "package main"
    for (i = 0; i < functions; i++) {
        printf "func bench%d(value i64) i64 { return value + %d }\n", i, i
    }
    print "func main() {"
    print "    var total i64 = 0"
    for (i = 0; i < functions; i++) {
        printf "    total += bench%d(1)\n", i
    }
    print "    println(total)"
    print "}"
}' > "$SOURCE"

printf '%s\n' 'Kremen check benchmark (each process startup and fork/exec/wait included)'
printf 'host: %s\n' "$(uname -sm)"
printf 'native: %s\n' "$("$COMPILER" --version)"
printf 'native binary POSIX cksum: %s\n' "$(cksum < "$COMPILER")"
compiler_config="$(dirname -- "$COMPILER")/config"
if [ -r "$compiler_config" ]; then
    printf 'native build config: %s\n' "$(cat "$compiler_config")"
fi
printf '%s\n' 'timer: bench/time.c, POSIX CLOCK_MONOTONIC and child CPU usage'
printf 'functions: %s + main; runs: %s; warmup: 1 per compiler\n' "$FUNCTIONS" "$RUNS"
printf 'source bytes: %s; POSIX cksum: %s\n' "$(wc -c < "$SOURCE" | awk '{print $1}')" "$(cksum < "$SOURCE")"
printf '%s\n' 'operation: check only; no C generation, external C compiler, link, or program execution'

# Warm caches and validate before timing. Every timed process checks the same file.
"$COMPILER" check "$SOURCE" > /dev/null
if ! "$TIMER" "$RUNS" "$COMPILER" check "$SOURCE" > /dev/null 2> "$WORK/native.time"; then
    cat "$WORK/native.time" >&2
    exit 1
fi
printf '%s\n' 'compiler  total elapsed(s)  mean/process(s)  user(s)  sys(s)'
awk -v runs="$RUNS" 'BEGIN {real=user=sys=0}
    $1=="real" {real=$2} $1=="user" {user=$2} $1=="sys" {sys=$2}
    END {printf "native    %.3f             %.6f         %.3f    %.3f\n",real,real/runs,user,sys}
' "$WORK/native.time"

printf '%s\n' 'The mean is derived from a batch wall-clock measurement; individual runs and statistical uncertainty are not sampled.'
printf '%s\n' 'This measures this frontend workload on this host; it does not measure generated-program speed.'
