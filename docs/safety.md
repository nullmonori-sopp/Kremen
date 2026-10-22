# Safety and memory in Kremen

Kremen checks many mistakes before a program runs and checks others while it runs. These checks are useful, but they do not make every possible program correct. This page explains the actual boundaries of the full language in 0.4.2. The self-hosted subset has a different, smaller set of checks.

If you are new to memory, begin with [tutorial lesson 9](tutorial.md#9-understand-ownership-before-using-dynamic-memory). A variable owns a value. A reference borrows access to that value. A slice describes storage owned by a runtime allocation. Copying a slice does not copy its elements or extend the lifetime of that storage.

## What the compiler checks

The frontend checks declarations, types, argument counts, operators, conditions, return paths, and constant ranges. Unused bindings, shadowed names, unreachable statements, and unhandled expression results are errors. `_name` and `_ = expression` let code express intentional non-use.

A borrow covers an entire named variable. Multiple shared references allow reading. A mutable reference excludes conflicting reads and writes. Local loans end with their lexical block, not at their last use. An argument's temporary loan normally ends after the call; `defer` keeps it until cleanup. A raw conversion preserves the loan and whether it came from a shared or mutable reference.

References and pointers cannot be returned, reassigned, or stored in composite values. This version has no lifetime parameters. Its conservative rules reject some programs that a more expressive lifetime system could prove safe.

The compiler also checks enum matches for coverage, duplicate variants, payload types, and arm order. Tuple indices must be constants. A result needs explicit initialization; zero-filled storage cannot silently create an invalid `!T`.

## What the runtime checks

Checked operations include integer overflow, division by zero, narrowing conversions, invalid shifts, array/slice indices, view bounds, failed-result access, stale slices, repeated or partial release, and invalid arena state. The checks remain active under `-O3`.

A slice assignment rechecks its owner after evaluating the right-hand side. This matters when a function on that side can free or reset the allocation: an address checked earlier must not become an unchecked stale write.

Allocation owners stay registered until process exit, even after their data is released. Old descriptor copies can therefore be identified without reading freed data. Arena and closed-file metadata are similarly retained. The metadata cost grows with allocations and opens; releasing payload storage does not reclaim every bookkeeping object immediately.

An arena reset or release invalidates all slices issued from it. A slice may escape its arena as a value, but it does not keep the arena alive; later access produces a runtime error. Individual arena slices cannot be freed separately.

## Cleanup and error handling

`!T`, `?`, `catch`, and result `match` use values and branches. Use them for expected failures. There is no heavy language exception mechanism.

A deferred call captures its arguments immediately. Its registration and captured values live on the generated C stack. Each block runs its registrations in reverse order on exit. A block without cleanup does not need to unwind a cleanup list on an ordinary return.

User panic, failed assertions, and runtime checks run active deferred calls, then terminate the process. A node is removed before its cleanup call executes, so a second panic cannot run that same node again. Recovery is not implemented. Cleanup is not guaranteed for SIGKILL, an unhandled external signal, or a corrupted process.

`fileClose` is idempotent and suitable for deferred cleanup. `fsClose` returns an explicit error on repeated close. Both invalidate every copy of the handle. Operations on a closed handle return `EBADF` rather than using a closed C stream.

## What unsafe means

Manual allocation, release, manual arena operations, and pointer operations require `unsafe`. This marks code that needs additional care about memory and resources. It does not disable ordinary type, bounds, or tracked lifetime checks.

Raw memory does not acquire a complete validity proof. A non-null pointer may still be invalid. The language currently restricts address construction and storage substantially, but arbitrary external C code can have its own behavior. General FFI, stable foreign ABI, device registers, and a volatile/MMIO model are not implemented.

There is no threading or runtime synchronization. Do not assume these data structures are ready for concurrent use.

## Input, files, and resource use

Input and file strings, and directory-name strings, remain managed until process exit. There is no garbage collector, total memory quota, stack-exhaustion guarantee, or execution-time limit. File reads have a 64 MiB per-call limit; directory listing has a million-entry limit. These are specific safeguards, not a general resource sandbox.

NUL bytes are rejected in filesystem paths before calling the C API. File data may contain NUL. Filesystem operations follow host permissions and symlinks. The import-root restriction applies to source loading; it does not restrict the executing program's filesystem access. Writes may partially succeed and do not promise durable storage through `fsync`.

The compiler bounds its own source sizes, token counts, nesting, and arena usage. Bounded diagnostic excerpts avoid reading outside source storage on long or malformed lines. Compiler limits do not impose the same limits on a compiled program's own data.

## Toolchain and platform boundary

The full compiler and runtime currently target a hosted POSIX process with libc. Strict C flags help detect generator defects; they do not prove that the compiler is free of defects. [Validation](validation.md) describes the regression and sanitizer coverage.

The self-hosted subset checks types, calls, scopes, returns, and function-body arithmetic. It sequences operands and arguments, but it does not implement the full language's borrowing or memory checks. Do not transfer the full compiler's guarantees to it. See the [bootstrap reference](../selfhost/README.md).

The bootstrap driver uses separate process arguments without a shell. It protects source files, compiler resources, and its executable from accidental overwrite, including aliases through links. Output publication is atomic after successful compilation. Temporary directories are removed on normal and diagnostic exits. Hostile concurrent filesystem changes and abrupt process termination remain outside those guarantees.

Generated full-language programs retain source paths and bounded snippets for diagnostics. They can report a location without reopening the source. [Understanding errors](diagnostics.md) explains the format and its display limits.
