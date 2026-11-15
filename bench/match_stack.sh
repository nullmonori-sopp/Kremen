#!/bin/sh
# A focused debug-build stack measurement, with an optional older compiler.
set -eu
LC_ALL=C
export LC_ALL
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
COMPILER="$ROOT/build/kremen"
BEFORE=
usage() {
    printf '%s\n' 'Usage: sh bench/match_stack.sh [--compiler PATH] [--before PATH]'
    printf '%s\n' 'Requires a Linux shell, awk, and a C compiler supporting GCC -fstack-usage.'
    printf '%s\n' 'CC selects the C compiler; Kremen defaults to build/kremen. No older binary is bundled.'
}
while [ "$#" -gt 0 ]; do
    case "$1" in
        --compiler|--before)
            if [ "$#" -lt 2 ]; then usage >&2; exit 2; fi
            case "$1" in --compiler) COMPILER=$2 ;; --before) BEFORE=$2 ;; esac
            shift 2 ;;
        --help|-h) usage; exit 0 ;;
        *) usage >&2; exit 2 ;;
    esac
done
case "$COMPILER" in /*) ;; *) COMPILER="$(pwd)/$COMPILER" ;; esac
if [ -n "$BEFORE" ]; then
    case "$BEFORE" in /*) ;; *) BEFORE="$(pwd)/$BEFORE" ;; esac
fi
for candidate in "$COMPILER" ${BEFORE:+"$BEFORE"}; do
    if [ ! -x "$candidate" ]; then printf 'Compiler missing: %s\n' "$candidate" >&2; exit 1; fi
done
if [ "$(uname -s)" != Linux ]; then
    printf '%s\n' 'This stack-limit probe currently supports Linux; it is not a Windows performance measurement.' >&2
    exit 1
fi
WORK=$(mktemp -d "${TMPDIR:-/tmp}/kremen-match-stack.XXXXXX")
trap 'rm -rf "$WORK"' EXIT HUP INT TERM
awk 'BEGIN {
    print "type Choice enum { Empty; Data(values [100000]i64) }";
    print "func main() { var choice Choice";
    for(i=0;i<20;i++)print "match choice { Empty => {}; Data(_) => {} }";
    print "println(7)\n}";
}' > "$WORK/input.kr"
printf '7\n' > "$WORK/expected"
printf '%s\n' 'Kremen repeated-match stack probe'
printf 'host: %s\n' "$(uname -sm)"
"${CC:-cc}" --version | awk 'NR == 1 { print "C compiler: " $0 }'
printf '%s\n' 'C options: -std=c11 -pthread -Wall -Wextra -Wpedantic -Werror -O0 -fstack-usage'
printf '%s\n' 'fixture: enum { Empty; Data([100000]i64) }, one zero-initialized local, 20 matches ignoring payloads'
printf 'source POSIX cksum: %s\n' "$(cksum < "$WORK/input.kr")"
printf '%s\n' 'execution stack limit: 8192 KiB; expected stdout: 7 followed by newline'
measure() {
    label=$1
    native=$2
    printf '%s compiler: %s\n' "$label" "$("$native" --version)"
    "$native" emit-c "$WORK/input.kr" --mode=work -o "$WORK/$label.c"
    "${CC:-cc}" -std=c11 -pthread -Wall -Wextra -Wpedantic -Werror -O0 -fstack-usage \
        -c "$WORK/$label.c" -o "$WORK/$label.o"
    "${CC:-cc}" -pthread "$WORK/$label.o" -o "$WORK/$label"
    awk -F '\t' '$1 ~ /:kr_fn_0_0__4_main$/ && $3 == "static" { print $2; found = 1 }
        END { if (!found) exit 1 }' "$WORK/$label.su" > "$WORK/$label.bytes"
    printf '%s main frame bytes: %s\n' "$label" "$(cat "$WORK/$label.bytes")"
    status=0
    # Redirect the surrounding group too, so a baseline SIGSEGV stays in the
    # captured diagnostic. Disable core files for this deliberate crash probe.
    { (
        ulimit -c 0
        ulimit -s 8192
        program_status=0
        "$WORK/$label" || program_status=$?
        exit "$program_status"
    ); } > "$WORK/$label.out" 2> "$WORK/$label.err" || status=$?
    matches=no
    if cmp -s "$WORK/expected" "$WORK/$label.out"; then matches=yes; fi
    printf '%s execution status: %s; expected stdout: %s\n' "$label" "$status" "$matches"
    if [ "$label" = current ] && { [ "$status" -ne 0 ] || [ "$matches" != yes ] || [ -s "$WORK/$label.err" ]; }; then
        cat "$WORK/$label.err" >&2
        exit 1
    fi
}
if [ -n "$BEFORE" ]; then measure before "$BEFORE"; fi
measure current "$COMPILER"
if [ -n "$BEFORE" ]; then
    awk 'NR == 1 { before = $1; next } { printf "main frame reduction: %.2f%%\n", 100 * (before - $1) / before }' \
        "$WORK/before.bytes" "$WORK/current.bytes"
fi
printf '%s\n' 'This measures the generated main frame for one -O0 workload, not total stack or application throughput.'
printf '%s\n' 'Frame sizes depend on the C compiler, ABI and options; no general -O3 speedup is demonstrated.'
