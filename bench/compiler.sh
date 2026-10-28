#!/bin/sh
# Compare complete emit-c processes with a user-supplied reference compiler.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
if [ "$#" -lt 1 ] || [ "$#" -gt 3 ]; then
    printf 'usage: sh bench/compiler.sh REFERENCE_COMPILER [RUNS] [FUNCTIONS]\n' >&2
    exit 2
fi
REFERENCE=$1
CURRENT=${KREMEN_BENCH_CURRENT:-"$ROOT/build/kremen"}
case "$REFERENCE" in /*) ;; *) REFERENCE="$(pwd)/$REFERENCE" ;; esac
case "$CURRENT" in /*) ;; *) CURRENT="$(pwd)/$CURRENT" ;; esac
RUNS=${2:-5}
FUNCTIONS=${3:-3000}
case "$RUNS" in ''|*[!0-9]*) printf 'RUNS must be a positive integer\n' >&2; exit 2 ;; esac
case "$FUNCTIONS" in ''|*[!0-9]*) printf 'FUNCTIONS must be a positive integer\n' >&2; exit 2 ;; esac
[ "${#RUNS}" -le 4 ] && [ "${#FUNCTIONS}" -le 5 ] || exit 2
[ "$RUNS" -ge 1 ] && [ "$RUNS" -le 1000 ] && [ "$FUNCTIONS" -ge 1 ] && [ "$FUNCTIONS" -le 10000 ] || exit 2
WORK=$(mktemp -d "${TMPDIR:-/tmp}/kremen-compiler-bench.XXXXXX")
trap 'rm -rf "$WORK"' EXIT HUP INT TERM
"${CC:-cc}" -std=c11 -Wall -Wextra -Wpedantic -Werror -O2 "$ROOT/bench/time.c" -o "$WORK/time"
awk -v count="$FUNCTIONS" 'BEGIN {
    for(i=0;i<count;i++) printf "func calculate%d(value i64) i64 { return value + %d }\n", i,i;
    print "func main() { var checksum i64 = 0";
    for(i=0;i<count;i++) printf "checksum += calculate%d(%d)\n", i,i;
    print "println(checksum) }";
}' > "$WORK/corpus.kr"
printf 'Reference: '; "$REFERENCE" --version
printf 'Current: '; "$CURRENT" --version
printf 'Host: '; uname -srm
printf 'Functions: %s\nRuns per batch: %s\n' "$FUNCTIONS" "$RUNS"
printf 'Input cksum: '; cksum "$WORK/corpus.kr" | awk '{print $1, $2}'
"$REFERENCE" emit-c "$WORK/corpus.kr" -o "$WORK/reference.c" > /dev/null
"$CURRENT" emit-c "$WORK/corpus.kr" -o "$WORK/current.c" > /dev/null
cmp "$WORK/reference.c" "$WORK/current.c"
printf 'Generated C is byte-identical.\n'
round=1
while [ "$round" -le 3 ]; do
    if [ "$round" -eq 2 ]; then order='current reference'; else order='reference current'; fi
    for version in $order; do
        if [ "$version" = reference ]; then compiler=$REFERENCE; else compiler=$CURRENT; fi
        printf '\nRound %s, %s\n' "$round" "$version"
        "$WORK/time" "$RUNS" "$compiler" emit-c "$WORK/corpus.kr" -o "$WORK/$version.c" 2>&1
    done
    cmp "$WORK/reference.c" "$WORK/current.c"
    round=$((round + 1))
done
