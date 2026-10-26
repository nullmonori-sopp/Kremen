#!/bin/sh
# A compiler must reject special source files before a blocking read can occur.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
NATIVE=${1:-"$ROOT/build/kremen"}
BOOTSTRAP=${2:-"$ROOT/selfhost/build/stage2"}
DRIVER=${3:-"$ROOT/selfhost/build/kremen-selfhost"}
WORK=$(mktemp -d "${TMPDIR:-/tmp}/kremen-source-io.XXXXXX")
trap 'rm -rf "$WORK"' EXIT HUP INT TERM
TOTAL=0
reject_regular() {
    name=$1; shift
    status=0
    timeout 5 "$@" > "$WORK/output" 2> "$WORK/error" || status=$?
    if [ "$status" -eq 0 ] || [ "$status" -eq 124 ]; then
        printf 'FAIL: %s: accepted or blocked on a FIFO (status %s)\n' "$name" "$status" >&2; exit 1
    fi
    grep -F 'regular file' "$WORK/error" > /dev/null || { cat "$WORK/error" >&2; exit 1; }
    TOTAL=$((TOTAL + 1))
    printf 'ok source-io.%s - %s\n' "$TOTAL" "$name"
}
mkfifo "$WORK/pipe.kr" "$WORK/pipe.krh"
reject_regular 'native check rejects FIFO source' "$NATIVE" check "$WORK/pipe.kr"
reject_regular 'native formatter rejects FIFO source' "$NATIVE" fmt "$WORK/pipe.kr"
reject_regular 'native formatter rejects FIFO header' "$NATIVE" fmt "$WORK/pipe.krh"
mkdir "$WORK/module"
mkfifo "$WORK/module/dependency.kr"
cat > "$WORK/module/main.kr" <<'KR'
import "dependency"
func main() {}
KR
reject_regular 'native imports reject FIFO source' "$NATIVE" check "$WORK/module/main.kr"
printf 'previous artifact\n' > "$WORK/existing"
reject_regular 'bootstrap translator rejects FIFO source' "$BOOTSTRAP" "$WORK/pipe.kr" "$WORK/existing"
reject_regular 'selfhost driver rejects FIFO source' "$DRIVER" build "$WORK/pipe.kr" -o "$WORK/existing"
grep -Fx 'previous artifact' "$WORK/existing" > /dev/null
if find "$WORK" -type d -name '*.kremen-*' | grep .; then
    printf 'FAIL: selfhost left a temporary directory after a frontend error\n' >&2; exit 1
fi
printf '\n%s source input/cleanup checks passed.\n' "$TOTAL"
