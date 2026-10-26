#!/bin/sh
# Exercise the Kremen compiler/driver and C OS boundary under ASan + UBSan.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$ROOT"
make selfhost
BUILD="$ROOT/build/sanitize-selfhost"
mkdir -p "$BUILD"
COMPILER=${CC:-cc}
ASAN_OPTIONS=${ASAN_OPTIONS:-detect_leaks=0}
UBSAN_OPTIONS=${UBSAN_OPTIONS:-halt_on_error=1}
export ASAN_OPTIONS UBSAN_OPTIONS
"$COMPILER" -std=c11 -Wall -Wextra -Wpedantic -Werror -O1 -g \
    -fsanitize=address,undefined -fno-sanitize-recover=all \
    selfhost/stage0.c selfhost/runtime.c selfhost/driver.c -o "$BUILD/stage0"
"$BUILD/stage0" selfhost/bootstrap.kr "$BUILD/compiler.c"
cmp selfhost/stage0.c "$BUILD/compiler.c"
"$BUILD/stage0" selfhost/toolchain.kr "$BUILD/toolchain.c"
"$COMPILER" -std=c11 -Wall -Wextra -Wpedantic -Werror -O1 -g \
    -fsanitize=address,undefined -fno-sanitize-recover=all \
    -Dkr_main=kr_bootstrap_entry -c "$BUILD/compiler.c" -o "$BUILD/compiler.o"
"$COMPILER" -std=c11 -Wall -Wextra -Wpedantic -Werror -O1 -g \
    -fsanitize=address,undefined -fno-sanitize-recover=all \
    "$BUILD/compiler.o" "$BUILD/toolchain.c" selfhost/runtime.c selfhost/host.c \
    selfhost/driver.c -o "$BUILD/kremen-selfhost"
KREMEN_SELFHOST_ROOT="$ROOT/selfhost"
export KREMEN_SELFHOST_ROOT
sh tests/selfhost/semantics.sh "$BUILD/stage0"
sh tests/selfhost/toolchain.sh "$BUILD/kremen-selfhost"
SANITIZE_GENERATED=1 sh tests/selfhost/frontend_stability.sh "$BUILD/stage0"
SANITIZE=1 sh tests/selfhost/runtime_stability.sh
SANITIZE=1 sh tests/selfhost/diagnostics.sh "$BUILD/kremen-selfhost"
printf '%s\n' 'ASan/UBSan passed: bootstrap compiler, typed semantic checks and selfhost driver/OS integration.'
