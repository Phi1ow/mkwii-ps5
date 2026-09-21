/* Returns a nonzero mask if a context switch corrupts a SysV nonvolatile GPR.
 * Both contexts use different values, so a missing save/restore cannot pass
 * merely because the caller happened not to modify that register. */
    .text
    .globl mkw_test_context_registers
mkw_test_context_registers:
    push %rbp
    push %rbx
    push %r12
    push %r13
    push %r14
    push %r15
    sub $24, %rsp
    mov %rdx, (%rsp)
    mov %rdx, %rbp
    mov %rdx, %rbx
    mov %rdx, %r12
    mov %rdx, %r13
    mov %rdx, %r14
    mov %rdx, %r15
    call mkw_ps5_context_switch
    mov (%rsp), %rdx
    xor %eax, %eax
    cmp %rdx, %rbp
    setne %al
    cmp %rdx, %rbx
    setne %cl
    shl $1, %cl
    or %cl, %al
    cmp %rdx, %r12
    setne %cl
    shl $2, %cl
    or %cl, %al
    cmp %rdx, %r13
    setne %cl
    shl $3, %cl
    or %cl, %al
    cmp %rdx, %r14
    setne %cl
    shl $4, %cl
    or %cl, %al
    cmp %rdx, %r15
    setne %cl
    shl $5, %cl
    or %cl, %al
    add $24, %rsp
    pop %r15
    pop %r14
    pop %r13
    pop %r12
    pop %rbx
    pop %rbp
    ret
#ifndef _WIN32
    .section .note.GNU-stack,"",@progbits
#endif
