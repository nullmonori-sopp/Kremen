# Integer math

The first bundled Kremen library provides small integer operations. It is written in ordinary Kremen, has no imports of its own, and does not allocate memory or use `unsafe`. It works in both work and learn.

Kremen 0.4.9 has integer types; it does not have floating-point types yet. This library therefore provides integer arithmetic and an integer square root. It does not provide trigonometry or decimal calculations.

## Import the library

Put this complete program in `main.kr`:

```go
package main

import "std/math"

func main() {
    println(math.min(12, 8))
    println(math.gcd(48, 18))
    println(math.pow(2, 10) catch code => -code)
}
```

With an installed compiler, run:

```sh
kremen run main.kr
```

It prints:

```text
8
6
1024
```

`import "std/math"` makes the library available through the name `math`. A call such as `math.gcd(48, 18)` selects the library's `gcd` function. You can choose another name with `import numbers "std/math"` and then call `numbers.gcd(48, 18)`.

The compiler reads the library from the Kremen installation. It does not download anything, search an online registry, or require a project-local copy. Keep the installation's `share/` directory beside `bin/`. In a source checkout the library lives in `stdlib/math/`; the installed copy lives in `share/kremen/stdlib/math/`.

`std/` selects bundled standard-library sources. Ordinary imports such as `import "geometry/vec"` still refer to your own project's files. [Files and imports](language.md#files-and-local-imports) explains the source boundaries.

## Success and failure are values

An `i64` holds a signed integer from `-9223372036854775808` to `9223372036854775807`. Some mathematically valid answers cannot fit in that range. For example, adding one to the largest `i64` needs a larger type.

`math.add` returns `!i64`: either a successful integer or a positive failure code. It checks the range before performing an operation that could overflow. You can handle the result with `match`:

```go
package main

import "std/math"

func main() {
    var answer = math.add(9223372036854775807, 1)
    match answer {
        ok(value) => {
            println("answer:", value)
        }
        fail(code) => {
            if code == 2001 {
                println("The answer is too large for i64.")
            } else {
                println("Calculation failed:", code)
            }
        }
    }
}
```

This prints `The answer is too large for i64.` and completes normally. A result lets your program decide what to do next. By comparison, an overflowing built-in `+` reports a runtime safety failure and stops that thread.

`catch` is useful when a fallback is meaningful. For example, `math.isqrt(81) catch code => -code` produces `9`; a negative input produces `-2004`. The fallback must have the success type, here `i64`. Inside a function returning `!i64`, `?` can pass a failure back to the caller. See [Error results](language.md#error-results) for the complete rules.

## Function reference

All parameters are `i64`. The table shows their names in call order.

| Function | Result | Behavior |
| --- | --- | --- |
| `min(left, right)` | `i64` | Smaller input |
| `max(left, right)` | `i64` | Larger input |
| `clamp(value, lower, upper)` | `!i64` | Keep `value` in the inclusive interval from `lower` to `upper`; fail with `2002` if the bounds are reversed |
| `magnitude(value)` | `u64` | Nonnegative magnitude, including the magnitude of the smallest `i64` |
| `abs(value)` | `!i64` | Nonnegative signed value; fail with `2001` for the smallest `i64` |
| `add(left, right)` | `!i64` | Signed addition; fail with `2001` if the answer is outside `i64` |
| `mul(left, right)` | `!i64` | Signed multiplication; fail with `2001` if the answer is outside `i64` |
| `gcd(left, right)` | `u64` | Greatest common divisor of the inputs' magnitudes; `gcd(0, 0)` is `0` |
| `lcm(left, right)` | `!i64` | Nonnegative least common multiple; `0` if either input is zero; fail with `2001` if it cannot fit in `i64` |
| `pow(base, exponent)` | `!i64` | Integer power; fail with `2003` for a negative exponent or `2001` for overflow; `0^0` is `1` |
| `isqrt(value)` | `!i64` | Floor of the square root; fail with `2004` for a negative input |

For example, `isqrt(15)` succeeds with `3`: `3 * 3` is at most `15`, while `4 * 4` is greater. This avoids floating-point rounding.

`magnitude` and `gcd` return `u64` because the magnitude of `-9223372036854775808` is `9223372036854775808`. That value fits in `u64` but not `i64`. Kremen requires an explicit, checked conversion if you later need a signed value.

These four failure codes belong to this library. They are not operating-system `errno` values; `errorMessage` does not translate them. Use the code table above to choose your program's response.

## Implementation and limits

The paired [API declarations](../stdlib/math/math.krh) list the exact signatures. The [implementation](../stdlib/math/math.kr) uses Euclid's algorithm for `gcd`, repeated squaring for `pow`, and a bounded integer search for `isqrt`. Operations use fixed-size integer values and no heap allocation. Their loops still depend on the inputs; this is not a constant-time cryptography library.

The [runnable example](../examples/math.kr) covers ordinary use. The math regressions check boundary values and failure codes in both semantic profiles and at `-O0` and `-O3`. They supplement the language's general runtime checks; they do not establish every possible program that uses the library as correct.
