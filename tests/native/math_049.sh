#!/bin/sh
# Exercise the shipped pure-Kremen math module in both checking profiles.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
KREMEN=${1:-"$ROOT/build/kremen"}
case "$KREMEN" in /*) ;; *) KREMEN="$(pwd)/$KREMEN" ;; esac
WORK=$(mktemp -d "${TMPDIR:-/tmp}/kremen-math049.XXXXXX")
trap 'rm -rf "$WORK"' EXIT HUP INT TERM
TOTAL=0
fail() { printf 'FAIL: %s\n' "$1" >&2; [ ! -f "$WORK/error" ] || cat "$WORK/error" >&2; exit 1; }
pass() { TOTAL=$((TOTAL + 1)); printf 'ok math049.%s - %s\n' "$TOTAL" "$1"; }

cat > "$WORK/input.kr" <<'KR'
package main
import "std/math"

func expectValue(actual !i64, expected i64) {
    match actual {
        ok(value) => { assert(value == expected) }
        fail(code) => {
            println("unexpected failure", code, "expected", expected)
            panic("math success expected")
        }
    }
}
func expectError(actual !i64, expected i64) {
    match actual {
        ok(value) => {
            println("unexpected success", value, "expected error", expected)
            panic("math failure expected")
        }
        fail(code) => { assert(code == expected) }
    }
}
func main() {
    const smallest i64 = -9223372036854775808
    const largest i64 = 9223372036854775807

    assert(math.min(smallest, largest) == smallest)
    assert(math.max(smallest, largest) == largest)
    assert(math.min(largest, smallest) == smallest)
    assert(math.max(largest, smallest) == largest)
    assert(math.min(-7, -7) == -7)
    assert(math.max(0, 0) == 0)
    println("min/max ordering and equality")

    expectValue(math.clamp(-4, -3, 8), -3)
    expectValue(math.clamp(9, -3, 8), 8)
    expectValue(math.clamp(2, -3, 8), 2)
    expectValue(math.clamp(-3, -3, 8), -3)
    expectValue(math.clamp(8, -3, 8), 8)
    expectValue(math.clamp(smallest, 4, 4), 4)
    expectValue(math.clamp(largest, smallest, largest), largest)
    expectValue(math.clamp(smallest, smallest, largest), smallest)
    expectError(math.clamp(0, 1, -1), 2002)
    expectError(math.clamp(0, largest, smallest), 2002)
    println("clamp endpoints and reversed bounds")

    assert(math.magnitude(smallest) == 9223372036854775808)
    assert(math.magnitude(largest) == 9223372036854775807)
    assert(math.magnitude(-1) == 1)
    assert(math.magnitude(0) == 0)
    expectValue(math.abs(-19), 19)
    expectValue(math.abs(0), 0)
    expectValue(math.abs(19), 19)
    expectValue(math.abs(-9223372036854775807), largest)
    expectValue(math.abs(largest), largest)
    expectError(math.abs(smallest), 2001)
    println("signed absolute value and full unsigned magnitude")

    expectValue(math.add(smallest, 0), smallest)
    expectValue(math.add(0, largest), largest)
    expectValue(math.add(smallest, largest), -1)
    expectValue(math.add(largest, smallest), -1)
    expectValue(math.add(smallest, 1), -9223372036854775807)
    expectValue(math.add(largest, -1), 9223372036854775806)
    expectValue(math.add(-6, 9), 3)
    expectError(math.add(largest, 1), 2001)
    expectError(math.add(1, largest), 2001)
    expectError(math.add(smallest, -1), 2001)
    expectError(math.add(-1, smallest), 2001)
    expectError(math.add(smallest, smallest), 2001)
    println("addition signed boundaries and overflow")

    expectValue(math.mul(smallest, 1), smallest)
    expectValue(math.mul(1, smallest), smallest)
    expectValue(math.mul(smallest, 0), 0)
    expectValue(math.mul(0, smallest), 0)
    expectValue(math.mul(largest, 1), largest)
    expectValue(math.mul(largest, -1), -9223372036854775807)
    expectValue(math.mul(-3, -7), 21)
    expectValue(math.mul(-3, 7), -21)
    expectValue(math.mul(3037000499, 3037000499), 9223372030926249001)
    expectError(math.mul(3037000500, 3037000500), 2001)
    expectError(math.mul(smallest, -1), 2001)
    expectError(math.mul(-1, smallest), 2001)
    expectError(math.mul(smallest, smallest), 2001)
    expectError(math.mul(largest, 2), 2001)
    expectError(math.mul(smallest, 2), 2001)
    println("multiplication signs zero and both overflow directions")

    assert(math.gcd(0, 0) == 0)
    assert(math.gcd(-84, 30) == 6)
    assert(math.gcd(30, -84) == 6)
    assert(math.gcd(-84, -30) == 6)
    assert(math.gcd(0, -7) == 7)
    assert(math.gcd(smallest, 0) == 9223372036854775808)
    assert(math.gcd(0, smallest) == 9223372036854775808)
    assert(math.gcd(smallest, smallest) == 9223372036854775808)
    assert(math.gcd(smallest, largest) == 1)
    assert(math.gcd(smallest, -1) == 1)
    println("greatest common divisor including unsigned boundary")

    expectValue(math.lcm(0, 0), 0)
    expectValue(math.lcm(smallest, 0), 0)
    expectValue(math.lcm(0, smallest), 0)
    expectValue(math.lcm(12, 18), 36)
    expectValue(math.lcm(-12, 18), 36)
    expectValue(math.lcm(12, -18), 36)
    expectValue(math.lcm(-12, -18), 36)
    expectValue(math.lcm(largest, largest), largest)
    expectValue(math.lcm(3037000499, 3037000500), 9223372033963249500)
    expectError(math.lcm(smallest, smallest), 2001)
    expectError(math.lcm(smallest, -1), 2001)
    expectError(math.lcm(-1, smallest), 2001)
    expectError(math.lcm(largest, 2), 2001)
    expectError(math.lcm(3037000500, 3037000501), 2001)
    println("least common multiple reduces before checked multiplication")

    expectValue(math.pow(0, 0), 1)
    expectValue(math.pow(smallest, 0), 1)
    expectValue(math.pow(0, largest), 0)
    expectValue(math.pow(1, largest), 1)
    expectValue(math.pow(-1, largest), -1)
    expectValue(math.pow(-1, largest - 1), 1)
    expectValue(math.pow(smallest, 1), smallest)
    expectValue(math.pow(largest, 1), largest)
    expectValue(math.pow(2, 62), 4611686018427387904)
    expectValue(math.pow(-2, 63), smallest)
    expectError(math.pow(2, 63), 2001)
    expectError(math.pow(-2, 64), 2001)
    expectError(math.pow(smallest, 2), 2001)
    expectError(math.pow(0, -1), 2003)
    expectError(math.pow(2, smallest), 2003)
    println("integer powers empty product large exponents and failures")

    expectValue(math.isqrt(0), 0)
    expectValue(math.isqrt(1), 1)
    expectValue(math.isqrt(2), 1)
    expectValue(math.isqrt(3), 1)
    expectValue(math.isqrt(4), 2)
    expectValue(math.isqrt(8), 2)
    expectValue(math.isqrt(9), 3)
    expectValue(math.isqrt(10), 3)
    expectValue(math.isqrt(9223372030926249000), 3037000498)
    expectValue(math.isqrt(9223372030926249001), 3037000499)
    expectValue(math.isqrt(largest), 3037000499)
    expectError(math.isqrt(-1), 2004)
    expectError(math.isqrt(smallest), 2004)
    println("integer square roots adjacent squares maximum and negatives")

    // Test defining inequalities independently of the binary-search algorithm.
    for input := 0; input < 4096; input++ {
        var root = math.isqrt(input) catch code => -code
        assert(root >= 0)
        assert(root * root <= input)
        assert((root + 1) * (root + 1) > input)
    }
    println("square root defining inequalities over 4096 inputs")

    // Brute-force common divisors, rather than duplicating Euclid's algorithm.
    for left := -16; left <= 16; left++ {
        for right := -16; right <= 16; right++ {
            var divisor = math.gcd(left, right)
            var multiple = math.lcm(left, right) catch code => -code
            var first = math.magnitude(left)
            var second = math.magnitude(right)
            assert(multiple >= 0)
            if divisor == 0 {
                assert(left == 0 && right == 0)
            } else {
                assert(first % divisor == 0 && second % divisor == 0)
                for candidate := u64(1); candidate <= 16; candidate++ {
                    if first % candidate == 0 && second % candidate == 0 {
                        assert(candidate <= divisor)
                    }
                }
            }
            assert(u64(multiple) * divisor == first * second)
            expectValue(math.add(left, right), left + right)
            expectValue(math.mul(left, right), left * right)
        }
    }
    println("gcd lcm and arithmetic identities over 1089 signed pairs")

    // A simple repeated product is an independent reference at safe magnitudes.
    for base := -5; base <= 5; base++ {
        var expected i64 = 1
        for exponent := 0; exponent <= 12; exponent++ {
            expectValue(math.pow(base, exponent), expected)
            expected *= base
        }
    }
    println("power compared with repeated products over 143 inputs")
}
KR

cat > "$WORK/expected" <<'OUT'
min/max ordering and equality
clamp endpoints and reversed bounds
signed absolute value and full unsigned magnitude
addition signed boundaries and overflow
multiplication signs zero and both overflow directions
greatest common divisor including unsigned boundary
least common multiple reduces before checked multiplication
integer powers empty product large exponents and failures
integer square roots adjacent squares maximum and negatives
square root defining inequalities over 4096 inputs
gcd lcm and arithmetic identities over 1089 signed pairs
power compared with repeated products over 143 inputs
OUT

for profile in work learn; do
    "$KREMEN" emit-c "$WORK/input.kr" --mode="$profile" -o "$WORK/program.c" > "$WORK/log" 2> "$WORK/error" || fail "emit $profile"
    for opt in -O0 -O3; do
        if [ "${SANITIZE_GENERATED:-0}" = 1 ]; then
            "${CC:-cc}" -std=c11 -pthread -Wall -Wextra -Wpedantic -Werror "$opt" -g \
                -fsanitize=address,undefined -fno-sanitize-recover=all "$WORK/program.c" -o "$WORK/program" 2> "$WORK/error" || fail "instrument $profile $opt"
        else
            "${CC:-cc}" -std=c11 -pthread -Wall -Wextra -Wpedantic -Werror "$opt" "$WORK/program.c" -o "$WORK/program" 2> "$WORK/error" || fail "compile $profile $opt"
        fi
        timeout 20 "$WORK/program" > "$WORK/output" 2> "$WORK/error" || fail "run $profile $opt"
        cmp "$WORK/expected" "$WORK/output" > /dev/null || fail "changed output $profile $opt"
        [ ! -s "$WORK/error" ] || fail "unexpected diagnostic $profile $opt"
        while IFS= read -r label; do pass "$label ($profile $opt)"; done < "$WORK/output"
    done
done
printf 'Math 0.4.9 checks passed: %s.\n' "$TOTAL"
