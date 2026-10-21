/* System V x86-64. Source and destination must not overlap. The ABI requires
 * a clear direction flag on entry; rep movsb leaves it unchanged. */
    .text
    .p2align 4
    .globl km_fast_copy
    .type km_fast_copy, @function
km_fast_copy:
    movq %rdi, %rax
    movq %rdx, %rcx
    rep movsb
    ret
    .size km_fast_copy, .-km_fast_copy

    .section .note.GNU-stack,"",@progbits
