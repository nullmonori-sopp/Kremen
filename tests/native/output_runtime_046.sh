#!/bin/sh
# Buffered writes must fail the process when their final flush fails. Linux's
# /dev/full provides a deterministic delayed failure without changing runtime
# buffering or treating sanitizer failures as a successful negative test.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
KREMEN=${1:-"$ROOT/build/kremen"}
case "$KREMEN" in /*) ;; *) KREMEN="$(pwd)/$KREMEN" ;; esac
if [ ! -c /dev/full ]; then
    printf 'SKIP output-runtime046: /dev/full is unavailable; no checks run.\n'
    exit 0
fi
WORK=$(mktemp -d "${TMPDIR:-/tmp}/kremen-output-runtime046.XXXXXX")
trap 'rm -rf "$WORK"' EXIT HUP INT TERM
TOTAL=0
fail() { printf 'FAIL: %s\n' "$1" >&2; cat "$WORK/error" >&2; exit 1; }
cat > "$WORK/marker.kr" <<'KR'
func marker(path string) {
    match fsWrite(path, "cleanup") {
        ok(written) => { assert(written) }
        fail(code) => { panic(errorMessage(code)) }
    }
}
KR
cat > "$WORK/main.kr" <<'KR'
import "marker"
func main() {
    defer marker.marker("main.marker")
    println("main output")
}
KR
cat > "$WORK/worker.kr" <<'KR'
import "marker"
func worker(value i64) i64 {
    defer marker.marker("worker.marker")
    println("worker output", value)
    return value
}
func main() {
    defer marker.marker("main.marker")
    match threadSpawn(worker, 7) {
        ok(_handle) => {}
        fail(code) => { panic(errorMessage(code)) }
    }
}
KR
cat > "$WORK/panic.kr" <<'KR'
import "marker"
func main() {
    defer marker.marker("main.marker")
    println("main output")
    panic("original failure")
}
KR
cat > "$WORK/worker-panic.kr" <<'KR'
import "marker"
func worker(value i64) i64 {
    defer marker.marker("worker.marker")
    println("worker output", value)
    panic("original failure")
}
func main() {
    defer marker.marker("main.marker")
    match threadSpawn(worker, 7) {
        ok(_handle) => {}
        fail(code) => { panic(errorMessage(code)) }
    }
}
KR
for fixture in main worker panic worker-panic; do
    "$KREMEN" emit-c "$WORK/$fixture.kr" -o "$WORK/program.c" > "$WORK/output" 2> "$WORK/error" || fail "$fixture emit-c"
    if [ "${SANITIZE_GENERATED:-0}" = 1 ]; then
        "${CC:-cc}" -std=c11 -pthread -Wall -Wextra -Wpedantic -Werror -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all \
            "$WORK/program.c" -o "$WORK/program" 2> "$WORK/error" || fail "$fixture sanitized C compilation"
    else
        "${CC:-cc}" -std=c11 -pthread -Wall -Wextra -Wpedantic -Werror -O3 \
            "$WORK/program.c" -o "$WORK/program" 2> "$WORK/error" || fail "$fixture C compilation"
    fi
    for sink in normal full; do
        rm -f "$WORK/main.marker" "$WORK/worker.marker"
        if [ "$sink" = full ]; then output=/dev/full; else output="$WORK/output"; fi
        status=0
        (cd "$WORK" && timeout 10 ./program > "$output" 2> "$WORK/error") || status=$?
        expected=0
        case "$fixture:$sink" in *panic:*|*:full) expected=1 ;; esac
        [ "$status" -eq "$expected" ] || fail "$fixture $sink exit status $status"
        [ "$(cat "$WORK/main.marker")" = cleanup ] || fail "$fixture $sink main deferred cleanup"
        case "$fixture" in worker*) [ "$(cat "$WORK/worker.marker")" = cleanup ] || fail "$fixture $sink worker deferred cleanup" ;; esac
        if [ "$sink" = full ]; then
            [ "$(grep -Fc 'runtime error: output failed during final flush' "$WORK/error")" -eq 1 ] || fail "$fixture $sink missing or repeated final flush report"
        else
            if grep -F 'output failed' "$WORK/error" > /dev/null; then fail "$fixture $sink false output failure"; fi
            case "$fixture" in worker*) expected_output='worker output 7' ;; *) expected_output='main output' ;; esac
            printf '%s\n' "$expected_output" > "$WORK/expected"
            cmp "$WORK/expected" "$WORK/output" || fail "$fixture $sink output"
        fi
        case "$fixture" in *panic) [ "$(grep -Fc 'runtime error: original failure' "$WORK/error")" -eq 1 ] || fail "$fixture $sink panic report" ;; esac
        if grep -E 'AddressSanitizer|UndefinedBehaviorSanitizer|LeakSanitizer' "$WORK/error" > /dev/null; then fail "$fixture $sink sanitizer report"; fi
        TOTAL=$((TOTAL + 1)); printf 'ok output-runtime046.%s - %s: %s sink, cleanup and exit status\n' "$TOTAL" "$fixture" "$sink"
    done
done
printf 'Output runtime 0.4.6 checks passed: %s.\n' "$TOTAL"
