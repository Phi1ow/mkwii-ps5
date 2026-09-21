#include "../runtime/context_x86_64.h"
#include <stdint.h>
#include <stdio.h>

enum { ROUNDS = 10000 };
static unsigned char stack_a[65536], stack_b[65536];
static void *scheduler_sp, *a_sp, *b_sp;
static volatile int visits_a, visits_b, errors;
static unsigned initial_mxcsr;
static unsigned short initial_cw;

extern unsigned MKW_CONTEXT_ABI mkw_test_context_registers(void**, void**, uint64_t);
static unsigned read_mxcsr(void) {
    unsigned v; __asm__ volatile("stmxcsr %0" : "=m"(v)); return v;
}
static unsigned short read_cw(void) {
    unsigned short v; __asm__ volatile("fnstcw %0" : "=m"(v)); return v;
}
static void set_controls(unsigned mxcsr, unsigned short cw) {
    __asm__ volatile("ldmxcsr %0; fldcw %1" : : "m"(mxcsr), "m"(cw));
}
static void check_controls(unsigned mxcsr, unsigned short cw) {
    if (read_mxcsr() != mxcsr || read_cw() != cw) ++errors;
}

static void MKW_CONTEXT_ABI worker_a(void* argument) {
    if (argument != (void*)(uintptr_t)0x1234) ++errors;
    check_controls(initial_mxcsr, initial_cw);
    const unsigned mxcsr = (initial_mxcsr & ~0x6000u) | 0x2000u;
    const unsigned short cw = (initial_cw & ~0x0c00u) | 0x0400u;
    set_controls(mxcsr, cw);
    for (;;) {
        ++visits_a;
        errors += mkw_test_context_registers(&b_sp, &a_sp, UINT64_C(0x1122334455667788)) != 0;
        check_controls(mxcsr, cw);
        errors += mkw_test_context_registers(&scheduler_sp, &a_sp, UINT64_C(0x123456789abcdef0)) != 0;
        check_controls(mxcsr, cw);
    }
}
static void MKW_CONTEXT_ABI worker_b(void* argument) {
    if (argument != (void*)(uintptr_t)0x5678) ++errors;
    check_controls(initial_mxcsr, initial_cw);
    const unsigned mxcsr = (initial_mxcsr & ~0x6000u) | 0x4000u;
    const unsigned short cw = (initial_cw & ~0x0c00u) | 0x0800u;
    set_controls(mxcsr, cw);
    for (;;) {
        ++visits_b;
        errors += mkw_test_context_registers(&a_sp, &b_sp, UINT64_C(0x8877665544332211)) != 0;
        check_controls(mxcsr, cw);
    }
}
int main(void) {
    initial_mxcsr = read_mxcsr(); initial_cw = read_cw();
    /* Deliberately unaligned tops exercise the ABI's alignment requirement. */
    a_sp = mkw_ps5_context_init(stack_a + sizeof(stack_a) - 3, worker_a, (void*)(uintptr_t)0x1234);
    b_sp = mkw_ps5_context_init(stack_b + sizeof(stack_b) - 7, worker_b, (void*)(uintptr_t)0x5678);
    for (int i = 0; i < ROUNDS; ++i) {
        errors += mkw_test_context_registers(&a_sp, &scheduler_sp, UINT64_C(0xfedcba9876543210)) != 0;
        check_controls(initial_mxcsr, initial_cw);
        if (visits_a != i + 1 || visits_b != i + 1) ++errors;
    }
    printf("SysV contexts: %d rounds, %d errors (registers, FP controls, arguments, nested yields)\n", ROUNDS, errors);
    return errors != 0;
}
