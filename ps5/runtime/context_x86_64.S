/* Cooperative x86-64 SysV contexts for WiiCompiled's guest scheduler.
 * Called at an ordinary function boundary: XMM registers and the red zone
 * are caller-saved. Preserve all six nonvolatile GPRs, x87 control and MXCSR.
 * This does not migrate contexts between OS threads or implement unwinding
 * across stacks. It makes no assumptions about a TLS segment register. */
    .text
    .globl mkw_ps5_context_switch
    .globl mkw_ps5_context_init

mkw_ps5_context_switch:
    push %rbp
    push %rbx
    push %r12
    push %r13
    push %r14
    push %r15
    sub $8, %rsp
    stmxcsr (%rsp)
    fnstcw 4(%rsp)
    mov %rsp, (%rsi)
    mov (%rdi), %rsp
    ldmxcsr (%rsp)
    fldcw 4(%rsp)
    add $8, %rsp
    pop %r15
    pop %r14
    pop %r13
    pop %r12
    pop %rbx
    pop %rbp
    ret

mkw_ps5_context_init:
    /* Frame: controls, r15, r14, entry/r13, argument/r12, rbx, rbp,
     * trampoline address. The trampoline gets a 16-byte-aligned call stack. */
    mov %rdi, %rax
    and $-16, %rax
    sub $64, %rax
    movq $0, 0(%rax)
    stmxcsr 0(%rax)
    fnstcw 4(%rax)
    movq $0, 8(%rax)
    movq $0, 16(%rax)
    mov %rsi, 24(%rax)
    mov %rdx, 32(%rax)
    movq $0, 40(%rax)
    movq $0, 48(%rax)
    lea .Lmkw_context_entry(%rip), %rcx
    mov %rcx, 56(%rax)
    ret

.Lmkw_context_entry:
    mov %r12, %rdi
    call *%r13
    /* HostContext's scheduler entry must terminate/yield explicitly. */
    ud2

#ifndef _WIN32
    .section .note.GNU-stack,"",@progbits
#endif
