# Working with threads

A thread is an independent path of execution inside a process. Each successful Kremen spawn creates one native POSIX thread. Two worker threads can run at the same time on different CPU cores. This helps when a program has independent work to do; starting a thread and joining it immediately does not by itself make a calculation faster.

Kremen 0.4.3 introduces a deliberately small threading interface. A worker receives one `i64` value and returns one `i64` value. It cannot capture a caller's local variables, borrow the caller's memory, or receive a slice or file handle. This gives the checker a clear boundary before more general shared-memory features are added.

This guide describes the full compiler, `kremen`. The self-hosted subset does not implement these operations.

## Start two independent calculations

The following is a complete program:

```go
package main

func square(value i64) i64 {
    return value * value
}

func calculate() !i64 {
    var left = threadSpawn(square, 12)?
    defer threadClose(left)
    var right = threadSpawn(square, 13)?
    defer threadClose(right)

    var first = threadJoin(left)?
    var second = threadJoin(right)?
    return ok(first + second)
}

func main() {
    println(calculate() catch code => -code)
}
```

On success it prints `313`: 144 plus 169. Save it as `workers.kr`, then run:

```sh
kremen run workers.kr
```

Read the calculation in order. The first spawn starts a worker for 12. The second starts another for 13 before the caller waits for either result. Both are allowed to run concurrently. The two joins then wait for completion and obtain their answers. The order in which workers finish is not specified; the joins and final addition are written in a predictable order.

The inputs are tiny to make the example readable. They are not a useful performance benchmark: thread creation costs much more than squaring a small integer.

## The three operations

| Operation | Result | Meaning |
| --- | --- | --- |
| `threadSpawn(worker, argument)` | `!thread` | Start a named Kremen `func(i64) i64` worker, or report a creation error |
| `threadJoin(handle)` | `!i64` | Wait for that worker and obtain its value or error |
| `threadClose(handle)` | No value | Join if needed and discard the value; suitable for `defer` |

The worker is supplied by name, without calling it: `threadSpawn(square, 12)`, not `threadSpawn(square(12), 12)`. A qualified imported function name is also allowed. Methods, external C functions, closures, and functions with a different signature are rejected.

Spawn and join errors use positive OS error codes. Their numeric values depend on the platform. Use `errorMessage(code)` for a description. `?` forwards those errors from a result-returning function; `catch` or `match` can handle them locally. No heavy exception mechanism is involved.

A successful join consumes the worker's join operation. Joining it a second time returns an error, even through a copied handle. `threadClose` is idempotent: it does nothing after the handle has already been joined. That is why the example can explicitly join and still keep a deferred close as a fallback.

## Why schedule a close immediately?

The second spawn can fail after the first has succeeded. A later `?` can also leave the calculation early. Registering `defer threadClose(handle)` after each successful spawn ensures any outstanding worker is joined before that block finishes.

`threadClose` cannot return a result because deferred calls return no value. If it must join a worker that failed, it reports a fatal cleanup error in the calling thread. Use `threadJoin` explicitly when a worker's failure is expected and should be handled as an ordinary result. After that join, the deferred close is harmless, including when the join returned the worker's panic error.

Outstanding children are also joined when their creator thread exits. This last-resort cleanup is about thread exit, not the lexical lifetime of a local variable. Use `defer` when you want the join to happen at a particular block boundary. There are no detached workers or forced cancellation.

A worker that never finishes can keep a join, automatic cleanup, or process exit waiting indefinitely. There is no timed join in this release. Design workers to finish, and avoid creating more threads than the amount of useful work justifies.

## Ownership and memory

A `thread` handle belongs to the thread that created it. It may be kept in a local variable or a local `!thread` result, and passed to a helper running in that same creator thread. It cannot be returned, borrowed through an address, or stored in an array, slice, tuple, struct, enum, or nested result. Copies refer to the same join operation; they are not independent workers.

The worker argument and return value are copied integers. Kremen's full language has no mutable global variables, and this API does not accept references, pointers, strings, slices, arenas, file handles, or other thread handles as worker arguments. There are no captured variables. A worker can create its own arrays, strings, allocations, files, arenas, or child workers using normal language operations.

Each thread has its own runtime allocation lists, file and arena bookkeeping, and defer stack. Cleanup of one worker does not run another thread's deferred calls or release the caller's resources. Worker-owned runtime storage is cleaned when that worker exits; the creator keeps the worker's small handle record until the creator exits, so stale copies can be checked safely.

The runtime limits each creator to 256 unjoined children and 65,536 retained handle records. Exceeding either limit makes spawn fail with `EAGAIN`. System resource limits may cause a failure sooner. Joining releases an active slot, but does not discard its retained handle record. An ownership hash index avoids repeatedly scanning those retained records; at its maximum capacity it uses 131,072 pointer slots, about 1 MiB on the tested 64-bit platform, in addition to the records themselves.

## Panic and failure inside a worker

A worker can fail a bounds check, overflow, assertion, or explicit `panic`. It reports the original source location and the integer argument with which that worker was started. It then runs its own active deferred calls, joins its own outstanding children, and cleans its own runtime resources. The creator's `threadJoin` returns `fail(ECANCELED)`.

This stops that worker; it is not panic recovery inside the worker. Other workers are not automatically canceled. A panic in the main thread still ends the process after its cleanup joins outstanding children. If a failed child is left unobserved until main-thread exit, the process exits unsuccessfully rather than silently treating it as success.

## Shared input, output, and files

The runtime serializes its standard-input readers and groups each `print` or `println` call's output. It does not promise a fixed order between calls from different workers. Prefer collecting integer results and printing them from the creator when reproducible output matters.

Two workers can still access the same filesystem path or an external device through the OS. Independent runtime bookkeeping does not make conflicting file operations a transaction or impose an application-level order. Give workers separate output files or arrange their work so they do not conflict.

Mutex values, atomics, channels, shared-memory slices, thread pools, timed joins, and a general `Send`/`Sync` type system are not implemented. This first API supports independent integer jobs; it is not a general shared-memory concurrency model.

## Building generated C yourself

The normal `build` and `run` commands add `-pthread` automatically. If you use `emit-c` and invoke the C compiler yourself, include it too:

```sh
kremen emit-c workers.kr -o workers.c
cc -std=c11 -Wall -Wextra -Wpedantic -Werror -O3 -pthread workers.c -o workers
./workers
```

The release targets hosted POSIX systems. Threads do not add a freestanding kernel target or remove the C toolchain and libc dependencies. See [Safety and memory](safety.md) for the wider boundary.
