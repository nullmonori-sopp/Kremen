# Learn Kremen by changing small programs

This tutorial continues from [Your first program](getting-started.md). Run commands from the unpacked `kremen` directory. The examples use `./build/kremen`; if you installed the toolchain, you can use `kremen` instead.

A **value** is a piece of information: a number, some text, or a true/false answer. An **expression** produces a value, such as `3 + 4`. A **statement** tells the program to do something, such as store a value or print it. You will see both throughout these lessons.

Each linked `.kr` file is a complete program. Shorter code blocks on this page explain a part of that program; they are not separate complete programs unless stated otherwise.

## 1. Make the computer say something

Open [01_hello.kr](../examples/tutorial/01_hello.kr):

```go
package main

func main() {
    println("Hello, Kremen!")
}
```

Run it:

```sh
./build/kremen run examples/tutorial/01_hello.kr
```

It prints `Hello, Kremen!`. `println` can take several arguments, separated by commas. It prints a space between them and a newline at the end. `print` works the same way without the final newline.

**Try it:** add a second `println` inside `main`. The calls run in the order you write them. Put text in double quotes; an unquoted word is interpreted as a name.

## 2. Remember values with variables

Run [02_variables.kr](../examples/tutorial/02_variables.kr):

```sh
./build/kremen run examples/tutorial/02_variables.kr
```

Expected output:

```text
apples: 5
cost: 20
```

The important lines are:

```go
var apples i64 = 3
const price i64 = 4
apples += 2
```

A **variable** is a named place for a value. `var` declares a variable. `apples` is its name, `i64` is its type, and `3` is its initial value. A **type** describes which values and operations are allowed. `i64` holds whole numbers, including negative ones, in 64 bits.

`const` declares a binding you cannot assign a new value to. We use it for `price` because this program does not change the price. The statement `apples += 2` means “add two to apples”; it has the same effect here as `apples = apples + 2`.

`=` assigns a value. `==` asks whether two values are equal. They do different jobs.

The compiler can often infer the type from the value:

```go
var apples = 3
apples := 3
```

These are two alternative ways to declare a new `i64` variable. Do not put both in the same block: they would declare the same name twice. Use `=` to update an existing variable; `:=` declares a new one.

`bool` stores `true` or `false`. `string` stores text as bytes. Smaller integer types such as `i32` and unsigned types such as `u8` are useful later when you need a particular representation. Kremen does not silently convert between integer types: `i64(smallNumber)` requests a checked conversion.

Unused names are errors. That rule catches mistakes such as computing a value and forgetting to use it. If a name is intentionally unused, start it with `_`, for example `_reserved`. Do not use this just to hide a problem you do not yet understand.

**Try it:** change the initial number of apples and predict the cost before running. Try assigning a new value to `price`, read the error, then remove that assignment.

## 3. Choose and repeat work

Run [03_control_flow.kr](../examples/tutorial/03_control_flow.kr):

```sh
./build/kremen run examples/tutorial/03_control_flow.kr
```

Expected output:

```text
even: 2
even: 4
total: 15
```

This loop visits the numbers 1 through 5:

```go
for number := 1; number <= 5; number++ {
    total += number
    if number % 2 == 0 {
        println("even:", number)
    }
}
```

Read the three parts of `for` as: start at 1; keep going while the number is at most 5; add 1 after each iteration. An **iteration** is one trip through the loop's body. `number++` adds one. The semicolons separate these three parts.

`if` runs its block only when its condition is `true`. `%` gives the remainder after division, so `number % 2 == 0` asks whether the number is even. `else` can provide the other case. Conditions must have type `bool`; a number by itself is not a condition.

`&&` means “and,” `||` means “or,” and `!` means “not.” The right side of `&&` is skipped when the left side is false; the right side of `||` is skipped when the left side is true.

A loop can also have just a condition, as in `for remaining > 0 { ... }`. `break` leaves the nearest loop; `continue` skips the rest of its current iteration. In a three-part loop, `continue` still runs its increment step.

**Try it:** include 6 by changing the condition. What should the total become? Change the program to print odd numbers instead of even ones.

## 4. Give a calculation a name

Run [04_functions.kr](../examples/tutorial/04_functions.kr):

```sh
./build/kremen run examples/tutorial/04_functions.kr
```

It prints `42`. Its helper function is:

```go
func twice(value i64) i64 {
    return value * 2
}
```

`value i64` is a **parameter**: a local name for an input supplied by a caller. The `i64` after `)` is the result type. `return` ends this call and sends its result back. `twice(21)` supplies the argument `21` and produces `42`.

A function without a result type, such as `main`, returns no value. Every possible path through a function with a result type must return an appropriate value. Parameters and ordinary local variables exist only within their scope; a function cannot read a caller's local names by accident.

**Try it:** write a `square(value i64) i64` function and call it from `main`. Make sure you print or otherwise use its result.

## 5. Group data in records and arrays

Run [05_records_arrays.kr](../examples/tutorial/05_records_arrays.kr):

```sh
./build/kremen run examples/tutorial/05_records_arrays.kr
```

Expected output:

```text
total: 12
original: 2 copy: 9
```

An **array** stores a fixed number of values of one type:

```go
var values = [3]i64{2, 4, 6}
```

`[3]i64` means three integers. Array positions, called **indices**, start at zero: `values[0]` is 2, `values[1]` is 4, and `values[2]` is 6. `len(values)` returns 3. There is no `values[3]`; the last valid index is the length minus one. Kremen checks bounds before access.

A **struct** gives a name to a group of fields:

```go
type Counter struct {
    value i64
}
```

A value such as `Counter{value: 0}` supplies the fields. A **method** is a function attached to that type:

```go
func (counter &mut Counter) add(amount i64) {
    counter.value += amount
}
```

The part before `add` is its **receiver**. `&mut Counter` lets the method change the caller's counter. You call it with `counter.add(2)`. A receiver written as `counter Counter` would receive a copy instead. Lesson 9 explains references.

Arrays and structs are values: assigning one to another copies it. The lesson changes `copy[0]`, and the original array stays unchanged. Slices, introduced later, behave differently because they describe shared storage.

**Try it:** change one array element and predict the total. Add a `reset` method that sets the counter's field to zero.

## 6. Ask for input without crashing on a typo

Run [06_input.kr](../examples/tutorial/06_input.kr):

```sh
./build/kremen run examples/tutorial/06_input.kr
```

When it asks for an integer, type `17` and press Enter:

```text
Enter an integer:
17
Next: 18
```

The `17` line above is your input, echoed by the terminal. The program itself prints the other two lines. You can supply input automatically from the shell:

```sh
printf '17\n' | ./build/kremen run examples/tutorial/06_input.kr
```

The `|` connects the first command's output to the second command's input.

`readIntResult()` returns a **result**: either a valid integer or an error code. `match` chooses a block based on which case occurred:

```go
match input {
    ok(number) => { println("Next:", number + 1) }
    fail(code) => { println("Input error:", code) }
}
```

Inside the first arm, `number` is the integer. Inside the second, `code` describes the failure. For this built-in, 1 means end of input, 2 means invalid format, 3 means the number is outside the integer range, and 4 means a stream error. These are input-reader codes, not filesystem error codes.

`readLine()` reads a line of text. `readInt()` is a shorter integer reader that stops the program on failure; use `readIntResult()` when you want your program to handle the mistake.

**Try it:** enter `hello` instead of an integer. It should print `Input error: 2`. Press Ctrl-D on an empty input line on Linux to try the end-of-input case.

## 7. Return an error as a value

Run [07_results.kr](../examples/tutorial/07_results.kr):

```sh
./build/kremen run examples/tutorial/07_results.kr
```

Expected output:

```text
41
7
```

`doubled` returns `!i64`, pronounced “result of i64.” Its successful path returns `ok(value * 2)`. Its failure path returns `fail(7)`. In this example, we choose 7 to mean “negative input.” Application error codes must be positive; their meanings are your program's choice.

`doubled(-1) catch 0` would produce the fallback value 0. `doubled(-1) catch code => code` instead uses the actual error code as its fallback. The fallback runs only if the operation failed, and its type must match the successful value's type.

The helper `next` uses `?`:

```go
func next(value i64) !i64 {
    var answer = doubled(value)?
    return ok(answer + 1)
}
```

If `doubled` succeeds, `?` extracts the integer into `answer`. If it fails, `?` immediately returns that error from `next`. That is why `next` also has a result type. This is a value check and an early return; it is not a heavy exception mechanism.

A result also has `.ok`, `.code`, and `.value` fields. Reading `.value` from a failure stops the program. Prefer `match`, `catch`, or `?` when you need the successful value.

**Try it:** call `next(-1) catch 99`. Then use `catch code => code` and see the original code travel through `next`.

## 8. Describe alternatives and return several values

Run [08_enums_tuples.kr](../examples/tutorial/08_enums_tuples.kr):

```sh
./build/kremen run examples/tutorial/08_enums_tuples.kr
```

Expected output:

```text
no reading
reading: 42
7 seven
```

Sometimes data has different possible shapes. An **enum** describes those alternatives:

```go
type Reading enum {
    Missing
    Value(number i64)
}
```

`Reading.Missing()` has no payload. `Reading.Value(42)` carries an integer. A `match` must cover every variant, or end with a fallback arm `_ => { ... }`. The compiler catches missing cases and duplicate cases. The name in `Value(number)` receives the payload for that arm.

A **tuple** groups a few values without giving the group a new type:

```go
var (number, label) = (7, "seven")
```

This creates a pair and gives names to its members. Its type is `(i64, string)`. A function can return that type when it needs to return two related values. You can also keep a tuple in a single variable and access `.0` and `.1`.

**Try it:** add a third enum variant and run `check` before updating `show`. Read the missing-case error, then handle your new variant.

## 9. Understand ownership before using dynamic memory

Run [09_memory.kr](../examples/tutorial/09_memory.kr):

```sh
./build/kremen run examples/tutorial/09_memory.kr
```

Expected output:

```text
number: 2
slice: 3 42
arena: 9
```

A **reference** accesses an existing variable without copying it. `&number` permits reading; `&mut number` permits changing it. `*reference` accesses the referenced value. While a mutable reference exists, you cannot separately read or change the original variable. That exclusivity helps prevent conflicting access.

In the lesson, the reference lives in a nested block:

```go
{
    var reference = &mut number
    *reference += 1
}
println("number:", number)
```

The borrow ends at `}`, so printing `number` afterwards is allowed. Kremen currently tracks borrows until the end of their lexical block, even if their last use occurs earlier. It tracks the whole variable, including all its fields.

A **slice** describes a run of elements in dynamic storage. `alloc[i64](3)` creates three zeroed integers. Copying a slice copies its descriptor, so both copies access the same storage. `slice(values, 1, 3)` creates a view of positions 1 and 2 without copying the elements.

Allocation and release require an `unsafe` block. `unsafe` means you are taking responsibility for a lower-level operation. It does not turn off bounds checks or make every pointer valid. Put `defer free(values)` immediately after allocation to release that allocation on block exit. Lesson 10 explains `defer` in detail.

An **arena** groups allocations that should end together. `arenablock memory { ... }` creates one and automatically releases it at block exit. `arenaAlloc[i64](memory, 2)` allocates in that arena. Do not individually `free` an arena slice. `arenaReset(memory)` discards all of its allocations but keeps the arena available for new ones. An old slice becomes invalid after reset or release, even if you copied the descriptor elsewhere.

Raw pointers use `*T`, are created from references with `raw`, and require `unsafe` for creation and dereferencing. They are an advanced topic covered in the [reference](language.md#references-and-raw-pointers). You do not need them for any program in this tutorial.

**Try it:** change a slice element through a copied descriptor and print it through the original. Read [Safety and memory](safety.md) before experimenting with manual release or pointers.

## 10. Work with a file and release its handle

This lesson creates or overwrites `kremen-tutorial.txt` in your current directory. Use a practice directory where that name is free. The source file is [10_files_cleanup.kr](../examples/tutorial/10_files_cleanup.kr).

```sh
./build/kremen run examples/tutorial/10_files_cleanup.kr
```

Expected output on a writable filesystem:

```text
saved bytes: 6
finished
```

`fsWrite(path, "hello\n")` writes five letters and one newline: six bytes. `fsOpen(path, "rb")` opens it for binary reading and returns a file **handle**, a value representing the open resource. `fsReadAll(handle)` reads its contents. These calls return results because a file can be missing, inaccessible, or on a full device.

The helper uses `?` after each fallible call. Once it has an open handle, it schedules cleanup:

```go
var handle = fsOpen(path, "rb")?
defer fileClose(handle)
```

`defer` records a call to perform when the current block ends. Its arguments are evaluated immediately. The call still runs if the function returns early with `?`. If several calls are deferred in one block, the newest runs first. A nested block runs its own cleanup when that block ends.

`fileClose` is a void, idempotent cleanup operation, so it is suitable for `defer`. `fsClose` instead returns a result for code that wants to inspect a close failure directly. Expected errors should use results; `panic` reports an unrecoverable error, runs active deferred calls, and exits the program. There is no panic recovery in this version.

**Try it:** change the text and predict the byte count. Give the function a path inside a nonexistent directory to exercise its failure arm. Remove your practice file when you are done.

## A practice project: a tiny score calculator

You now have the pieces for a small program of your own. Read an integer score with `readIntResult`, reject values below zero, double valid scores with a function, and print the result. Keep input handling and calculation in separate functions. Add a struct only if it helps represent your data.

Use `check` after small edits. If something fails, read the first error before changing more code. Format your file with `fmt -w`. When it works, build an executable and run it directly.

Continue with [local imports](language.md#files-and-local-imports) when one file becomes unwieldy. The [language reference](language.md) collects the exact rules, and [Understanding errors](diagnostics.md) is a useful companion while you practice.
