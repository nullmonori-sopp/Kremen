#!/bin/sh
set -eu

bootstrap_dir=$(CDPATH= cd "$(dirname "$0")" && pwd)
build=${KREMEN_SELFHOST_BUILD:-"$bootstrap_dir/build"}
compiler=${CC:-cc}
mkdir -p "$build"

compile() {
    "$compiler" -std=c11 -O2 -Wall -Wextra -Wpedantic -Werror \
        "$1" "$bootstrap_dir/runtime.c" "$bootstrap_dir/driver.c" -o "$2"
}

compile "$bootstrap_dir/stage0.c" "$build/stage0"
"$build/stage0" "$bootstrap_dir/bootstrap.kr" "$build/stage1.c"
compile "$build/stage1.c" "$build/stage1"
"$build/stage1" "$bootstrap_dir/bootstrap.kr" "$build/stage2.c"
cmp "$build/stage1.c" "$build/stage2.c"
cmp "$bootstrap_dir/stage0.c" "$build/stage1.c"
compile "$build/stage2.c" "$build/stage2"
"$build/stage2" "$bootstrap_dir/bootstrap.kr" "$build/stage3.c"
cmp "$build/stage2.c" "$build/stage3.c"

"$build/stage2" "$bootstrap_dir/example.kr" "$build/example.c"
compile "$build/example.c" "$build/example"
"$build/example" "$build/example.output"
printf '720|18|ok\n' > "$build/example.expected"
cmp "$build/example.expected" "$build/example.output"

printf 'func main() i64 {\n' > "$build/invalid.kr"
if "$build/stage2" "$build/invalid.kr" "$build/invalid.c" 2> "$build/invalid.error"; then
    printf '%s\n' 'error: unterminated block was accepted' >&2
    exit 1
fi
diagnostic=$(cat "$build/invalid.error")
case "$diagnostic" in
    *invalid.kr:2:1:*"expected '}' before end of file"*) ;;
    *) printf '%s\n' "error: unexpected diagnostic: $diagnostic" >&2; exit 1 ;;
esac

printf 'func main() i64 { return @ }\n' > "$build/invalid.kr"
if "$build/stage2" "$build/invalid.kr" "$build/invalid.c" 2> "$build/invalid.error"; then
    printf '%s\n' 'error: invalid token was accepted' >&2
    exit 1
fi
diagnostic=$(cat "$build/invalid.error")
case "$diagnostic" in
    *invalid.kr:1:26:*"unexpected character: @"*) ;;
    *) printf '%s\n' "error: unexpected diagnostic: $diagnostic" >&2; exit 1 ;;
esac

printf 'func main() i64 { return 010 }\n' > "$build/invalid.kr"
if "$build/stage2" "$build/invalid.kr" "$build/invalid.c" 2> "$build/invalid.error"; then
    printf '%s\n' 'error: ambiguous leading-zero integer was accepted' >&2
    exit 1
fi
diagnostic=$(cat "$build/invalid.error")
case "$diagnostic" in
    *"decimal integer literals must not have leading zeros"*) ;;
    *) printf '%s\n' "error: unexpected diagnostic: $diagnostic" >&2; exit 1 ;;
esac

printf 'func main() i64 { return ' > "$build/invalid.kr"
nesting=0
while [ "$nesting" -lt 200 ]; do
    printf '(' >> "$build/invalid.kr"
    nesting=$((nesting + 1))
done
printf '1' >> "$build/invalid.kr"
nesting=0
while [ "$nesting" -lt 200 ]; do
    printf ')' >> "$build/invalid.kr"
    nesting=$((nesting + 1))
done
printf ' }\n' >> "$build/invalid.kr"
if "$build/stage2" "$build/invalid.kr" "$build/invalid.c" 2> "$build/invalid.error"; then
    printf '%s\n' 'error: parser nesting limit was not enforced' >&2
    exit 1
fi
diagnostic=$(cat "$build/invalid.error")
case "$diagnostic" in
    *"parser nesting limit (128) exceeded"*) ;;
    *) printf '%s\n' "error: unexpected diagnostic: $diagnostic" >&2; exit 1 ;;
esac

printf '%s\n' 'Self-hosting fixed point verified: seed == stage1.c == stage2.c == stage3.c.'
printf '%s\n' 'Stage2 compiled and ran the example; diagnostic checks passed.'

# Link the compiler and its Kremen-written CLI in the same process. The C shim
# only initializes arguments, calls kr_main and maps its process exit status.
"$build/stage2" "$bootstrap_dir/toolchain.kr" "$build/toolchain.c"
"$compiler" -std=c11 -O2 -Wall -Wextra -Wpedantic -Werror \
    -Dkr_main=kr_bootstrap_entry -c "$build/stage2.c" -o "$build/compiler.o"
"$compiler" -std=c11 -O2 -Wall -Wextra -Wpedantic -Werror \
    "$build/compiler.o" "$build/toolchain.c" "$bootstrap_dir/runtime.c" \
    "$bootstrap_dir/host.c" "$bootstrap_dir/driver.c" -o "$build/kremen-selfhost.tmp"
mv "$build/kremen-selfhost.tmp" "$build/kremen-selfhost"
printf '%s\n' 'Kremen-written selfhost driver built: check, emit-c, build, run, bootstrap.'
