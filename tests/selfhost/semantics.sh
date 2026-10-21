#!/bin/sh
# The bootstrap frontend must reject type errors before the C backend is run.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
COMPILER=${1:-"$ROOT/selfhost/build/stage2"}
case "$COMPILER" in /*) ;; *) COMPILER="$(pwd)/$COMPILER" ;; esac
WORK=$(mktemp -d "${TMPDIR:-/tmp}/kremen-bootstrap-semantics.XXXXXX")
trap 'rm -rf "$WORK"' EXIT HUP INT TERM
TOTAL=0
pass() { TOTAL=$((TOTAL + 1)); printf 'ok semantics.%s - %s\n' "$TOTAL" "$1"; }
reject() {
    label=$1
    expected=$2
    cat > "$WORK/invalid.kr"
    printf 'previous output\n' > "$WORK/invalid.c"
    if "$COMPILER" "$WORK/invalid.kr" "$WORK/invalid.c" > "$WORK/stdout" 2> "$WORK/error"; then
        printf 'FAIL: %s was accepted\n' "$label" >&2; exit 1
    fi
    if ! grep -F "$expected" "$WORK/error" > /dev/null || ! grep -F 'invalid.kr:' "$WORK/error" > /dev/null; then
        printf 'FAIL: %s has wrong diagnostic\n' "$label" >&2; cat "$WORK/error" >&2; exit 1
    fi
    grep -Fx 'previous output' "$WORK/invalid.c" > /dev/null || { printf 'FAIL: type error overwrote output\n' >&2; exit 1; }
    pass "$label"
}
cat > "$WORK/valid.kr" <<'KR'
extern func rt_arg(index i64) string
extern func rt_write(path string, value string) i64
extern func rt_equal(left string, right string) bool
var ceiling i64 = 9223372036854775807
func main() i64 {
    var minimum i64 = -9223372036854775808
    if even(6) && ceiling > minimum && rt_equal(text(), "ok") {
        return rt_write(rt_arg(1), "semantic-ok\n")
    }
    return 1
}
func even(n i64) bool {
    if n == 0 { return true }
    return odd(n - 1)
}
func odd(n i64) bool {
    if n == 0 { return false }
    return even(n - 1)
}
func text() string { return "ok" }
KR
"$COMPILER" "$WORK/valid.kr" "$WORK/valid.c"
"${CC:-cc}" -std=c11 -O2 -Wall -Wextra -Wpedantic -Werror "$WORK/valid.c" "$ROOT/selfhost/runtime.c" "$ROOT/selfhost/driver.c" -o "$WORK/valid"
"$WORK/valid" "$WORK/output"
printf 'semantic-ok\n' > "$WORK/expected"
cmp "$WORK/expected" "$WORK/output"
pass 'typed globals, forward calls, mutual recursion, bool/string and i64 limits'
cat > "$WORK/trigraphs.kr" <<'KR'
extern func rt_arg(index i64) string
extern func rt_write(path string, value string) i64
extern func rt_len(value string) i64
func main() i64 {
    var text string = "??= ??/ ??' ??( ??) ??! ??< ??> ??-\n"
    if rt_len(text) != 36 { return 1 }
    return rt_write(rt_arg(1), text)
}
KR
"$COMPILER" "$WORK/trigraphs.kr" "$WORK/trigraphs.c"
"${CC:-cc}" -std=c11 -O2 -Wall -Wextra -Wpedantic -Werror "$WORK/trigraphs.c" "$ROOT/selfhost/runtime.c" "$ROOT/selfhost/driver.c" -o "$WORK/trigraphs"
"$WORK/trigraphs" "$WORK/trigraphs.output"
cat > "$WORK/trigraphs.expected" <<'EXPECTED'
??= ??/ ??' ??( ??) ??! ??< ??> ??-
EXPECTED
cmp "$WORK/trigraphs.expected" "$WORK/trigraphs.output"
pass 'all nine C trigraph sequences retain their original string bytes'
reject 'bool cannot initialize i64' 'type mismatch' <<'KR'
func main() i64 { var n i64 = true; return n }
KR
reject 'i64 cannot initialize bool' 'type mismatch' <<'KR'
func main() i64 { var n bool = 1; if n { return 1 }; return 0 }
KR
reject 'assignment requires matching types' 'type mismatch' <<'KR'
func main() i64 { var n i64 = 0; n = false; return n }
KR
reject 'if condition must be bool' 'type mismatch' <<'KR'
func main() i64 { if 1 { return 1 }; return 0 }
KR
reject 'for condition must be bool' 'type mismatch' <<'KR'
func main() i64 { for 1 { break }; return 0 }
KR
reject 'arithmetic requires i64' 'type mismatch' <<'KR'
func main() i64 { return true + false }
KR
reject 'logical operators require bool' 'type mismatch' <<'KR'
func main() i64 { if 1 && 2 { return 1 }; return 0 }
KR
reject 'logical negation requires bool' 'type mismatch' <<'KR'
func main() i64 { if !1 { return 1 }; return 0 }
KR
reject 'relational operators require i64' 'type mismatch' <<'KR'
func main() i64 { if "a" < "b" { return 1 }; return 0 }
KR
reject 'strings are not compared as C pointers' 'rt_equal' <<'KR'
func main() i64 { if "a" == "a" { return 1 }; return 0 }
KR
reject 'equality requires matching types' 'type mismatch' <<'KR'
func main() i64 { if true == 1 { return 1 }; return 0 }
KR
reject 'calls require all arguments' 'argument count' <<'KR'
func f(n i64) i64 { return n }
func main() i64 { return f() }
KR
reject 'calls reject extra arguments' 'too many arguments' <<'KR'
func f() i64 { return 0 }
func main() i64 { return f(1) }
KR
reject 'call arguments are typed' 'type mismatch' <<'KR'
func f(n bool) i64 { if n { return 1 }; return 0 }
func main() i64 { return f(1) }
KR
reject 'undefined functions fail in frontend' 'undefined function' <<'KR'
func main() i64 { return missing() }
KR
reject 'undefined variables fail in frontend' 'undefined name' <<'KR'
func main() i64 { return missing }
KR
reject 'local variables are not callable' 'not callable' <<'KR'
func main() i64 { var value i64 = 2; return value() }
KR
reject 'function values are not supported' 'only valid in calls' <<'KR'
func f() i64 { return 0 }
func main() i64 { var n i64 = f; return n }
KR
reject 'assignments require a variable' 'assignment requires' <<'KR'
func main() i64 { (1 + 2) = 4; return 0 }
KR
reject 'duplicate locals fail in frontend' 'duplicate' <<'KR'
func main() i64 { var n i64 = 1; var n i64 = 2; return n }
KR
reject 'local shadowing is explicit error' 'shadowed' <<'KR'
func main() i64 { var n i64 = 1; unsafe { var n i64 = 2; n = n + 1 }; return n }
KR
reject 'duplicate parameters fail in frontend' 'duplicate' <<'KR'
func f(n i64, n i64) i64 { return n }
func main() i64 { return f(1, 2) }
KR
reject 'duplicate functions fail in frontend' 'duplicate top-level' <<'KR'
func f() i64 { return 1 }
func f() i64 { return 2 }
func main() i64 { return f() }
KR
reject 'duplicate globals fail in frontend' 'duplicate top-level' <<'KR'
var n i64 = 1
var n i64 = 2
func main() i64 { return n }
KR
reject 'variables and functions share declaration namespace' 'duplicate top-level' <<'KR'
var f i64 = 1
func f() i64 { return 2 }
func main() i64 { return f() }
KR
reject 'entry point is required' 'requires func main() i64' <<'KR'
func other() i64 { return 0 }
KR
reject 'entry point must return i64' 'entry point must' <<'KR'
func main() bool { return true }
KR
reject 'entry point cannot have arguments' 'entry point must' <<'KR'
func main(n i64) i64 { return n }
KR
reject 'external entry point is invalid' 'entry point must' <<'KR'
extern func main() i64
KR
reject 'return values are typed' 'type mismatch' <<'KR'
func main() i64 { return true }
KR
reject 'all paths must return' 'may finish without returning' <<'KR'
func f(n bool) i64 { if n { return 1 } }
func main() i64 { return f(true) }
KR
reject 'inner locals do not escape their block' 'undefined name' <<'KR'
func main() i64 { unsafe { var n i64 = 1; n = n + 1 }; return n }
KR
reject 'global initializers cannot call functions' 'literal constant' <<'KR'
func f() i64 { return 1 }
var n i64 = f()
func main() i64 { return n }
KR
reject 'positive literal overflow' 'outside i64 range' <<'KR'
func main() i64 { return 9223372036854775808 }
KR
reject 'arbitrarily large integer is rejected' 'outside i64 range' <<'KR'
func main() i64 { return 99999999999999999999999999999999999 }
KR
reject 'boolean literals cannot be identifiers' 'reserved language' <<'KR'
func main() i64 { var true i64 = 1; return 0 }
KR
reject 'generated C macro names are reserved' 'reserved C header' <<'KR'
func INT64_C() i64 { return 1 }
func main() i64 { return 0 }
KR
reject 'stdint maximum macro cannot be a local' 'reserved C header' <<'KR'
func main() i64 { var INT64_MAX i64 = 1; return INT64_MAX }
KR
reject 'stdint minimum macro cannot be a local' 'reserved C header' <<'KR'
func main() i64 { var INT64_MIN i64 = 1; return INT64_MIN }
KR
reject 'unsigned constant macro cannot be a function' 'reserved C header' <<'KR'
func UINT64_C() i64 { return 1 }
func main() i64 { return 0 }
KR
reject 'stdint narrow integer typedef cannot be a local' 'reserved C header' <<'KR'
func main() i64 { var int8_t i64 = 1; return int8_t }
KR
reject 'stdint maximum integer typedef cannot be a function' 'reserved C header' <<'KR'
func intmax_t() i64 { return 1 }
func main() i64 { return 0 }
KR
reject 'stdint least-width typedef cannot be a local' 'reserved C header' <<'KR'
func main() i64 { var int_least16_t i64 = 1; return int_least16_t }
KR
reject 'stdint fast-width typedef cannot be a local' 'reserved C header' <<'KR'
func main() i64 { var uint_fast32_t i64 = 1; return uint_fast32_t }
KR
reject 'stdint pointer typedef cannot be a local' 'reserved C header' <<'KR'
func main() i64 { var uintptr_t i64 = 1; return uintptr_t }
KR
reject 'stdint size limit macro cannot be a local' 'reserved C header' <<'KR'
func main() i64 { var SIZE_MAX i64 = 1; return SIZE_MAX }
KR
reject 'stdbool implementation macro cannot be a local' 'reserved C header' <<'KR'
func main() i64 { var __bool_true_false_are_defined i64 = 1; return 0 }
KR
reject 'bool keyword cannot be a local' 'reserved language' <<'KR'
func main() i64 { var bool i64 = 1; return bool }
KR
reject 'loop exits require a loop' 'enclosing loop' <<'KR'
func main() i64 { break; return 0 }
KR
reject 'unreachable statements fail in frontend' 'unreachable statement' <<'KR'
func main() i64 { return 0; return 1 }
KR
reject 'literal calls cannot become adjacent expression statements' 'function call' <<'KR'
func main() i64 { 1(2); return 0 }
KR
printf '\n%s bootstrap semantic checks passed.\n' "$TOTAL"
