#!/bin/sh
# Reproducible checks of the native compiler and generated slice assignments.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$ROOT"
BUILD="$ROOT/build/sanitize"
mkdir -p "$BUILD"
COMPILER=${CC:-cc}
# LeakSanitizer is not supported under the managed environment's ptrace.
# On an unrestricted host: ASAN_OPTIONS=detect_leaks=1 make test-sanitize.
ASAN_OPTIONS=${ASAN_OPTIONS:-detect_leaks=0}
UBSAN_OPTIONS=${UBSAN_OPTIONS:-halt_on_error=1}
export ASAN_OPTIONS UBSAN_OPTIONS

"$COMPILER" -std=c11 -Wall -Wextra -Wpedantic -Werror -O1 -g \
    -fsanitize=address,undefined -fno-sanitize-recover=all \
    -DKM_RUNTIME_PATH="\"$ROOT/runtime/kremen_rt.h\"" \
    native/core.c native/lexer.c native/parser.c native/modules.c \
    native/checker.c native/codegen.c native/format.c native/main.c \
    native/fast_copy.c -o "$BUILD/kremen"
sh tests/native/run.sh "$BUILD/kremen"
sh tests/native/v3.sh "$BUILD/kremen"
sh tests/native/frontend_stability.sh "$BUILD/kremen"
sh tests/native/diagnostics.sh "$BUILD/kremen"
SANITIZE_GENERATED=1 sh tests/native/semantic_stability.sh "$BUILD/kremen"
SANITIZE=1 sh tests/native/runtime_stability.sh
SANITIZE=1 sh tests/native/toolchain_stability.sh "$BUILD/kremen"
SANITIZE_GENERATED=1 sh tests/native/v3.sh "$BUILD/kremen"

"$BUILD/kremen" emit-c tests/native/memory_order.kr -o "$BUILD/memory_order.c"
"$COMPILER" -std=c11 -Wall -Wextra -Wpedantic -Werror -O1 -g \
    -fsanitize=address,undefined -fno-sanitize-recover=all \
    "$BUILD/memory_order.c" -o "$BUILD/memory_order"
printf '0\n' | "$BUILD/memory_order" > "$BUILD/actual"
printf '2 9 0 0\n17\n' > "$BUILD/expected"
cmp "$BUILD/expected" "$BUILD/actual"
for mode in 1 2 3 4 5 6; do
    printf '%s\n' "$mode" > "$BUILD/input"
    if "$BUILD/memory_order" < "$BUILD/input" > "$BUILD/actual" 2> "$BUILD/error"; then
        printf 'error: stale assignment mode %s was accepted\n' "$mode" >&2
        exit 1
    fi
    if ! grep -F ': runtime error: ' "$BUILD/error" > /dev/null ||
       ! grep -F 'slice use after free' "$BUILD/error" > /dev/null; then
        cat "$BUILD/error" >&2
        exit 1
    fi
done
printf '%s\n' 'ASan/UBSan passed: native compiler/runtime/toolchain regressions, generated v0.3/borrow tests and 7 assignment-lifetime scenarios.'
