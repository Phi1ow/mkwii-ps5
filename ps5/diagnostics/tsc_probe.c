/* SPDX-License-Identifier: GPL-3.0-only
 * DIAGNOSTIC payload: can user code read the time-stamp counter directly?
 *
 * The function profiler (ps5/runtime/function_profiler_ps5.cpp) takes two
 * timestamps per translated call. A kernel call costs ~11 ns, the RDTSC
 * instruction a few ns, but RDTSC faults when the kernel sets CR4.TSD. This
 * payload runs through the ELF loader, so a fault only ends the payload:
 * the klog then shows "rdtsc begin" without "rdtsc ok". It compares the raw
 * instruction with sceKernelReadTsc and the process time counter.
 */
extern void __prospero_klog(const char*);
extern unsigned long sceKernelReadTsc(void);
extern unsigned long sceKernelGetTscFrequency(void);
extern unsigned long sceKernelGetProcessTimeCounter(void);
extern unsigned long sceKernelGetProcessTimeCounterFrequency(void);

static char line[256];
static unsigned pos;
static void text(const char* s) { while (*s && pos < sizeof(line) - 2) line[pos++] = *s++; }
static void dec(unsigned long v) {
    char digits[24];
    unsigned n = 0;
    do { digits[n++] = (char)('0' + v % 10); v /= 10; } while (v && n < sizeof(digits));
    while (n && pos < sizeof(line) - 2) line[pos++] = digits[--n];
}
static void flush(void) { line[pos++] = '\n'; line[pos] = 0; __prospero_klog(line); pos = 0; }

__attribute__((noinline)) static unsigned long raw_rdtsc(void) { return __builtin_ia32_rdtsc(); }

int main(void) {
    enum { N = 1000000 };
    unsigned long f = sceKernelGetTscFrequency(), pf = sceKernelGetProcessTimeCounterFrequency();
    text("[mkw-tsc] tsc-frequency "); dec(f); text(" counter-frequency "); dec(pf); flush();

    unsigned long c0 = sceKernelGetProcessTimeCounter(), sink = 0;
    for (int i = 0; i < N; ++i) sink += sceKernelReadTsc();
    unsigned long c1 = sceKernelGetProcessTimeCounter();
    text("[mkw-tsc] sceKernelReadTsc ps/call "); dec((c1 - c0) * 1000000ul / pf); flush();

    c0 = sceKernelGetProcessTimeCounter();
    for (int i = 0; i < N; ++i) sink += sceKernelGetProcessTimeCounter();
    c1 = sceKernelGetProcessTimeCounter();
    text("[mkw-tsc] ProcessTimeCounter ps/call "); dec((c1 - c0) * 1000000ul / pf); flush();

    text("[mkw-tsc] rdtsc begin"); flush();
    unsigned long a = raw_rdtsc(), k = sceKernelReadTsc(), b = raw_rdtsc();
    text("[mkw-tsc] rdtsc ok raw "); dec(a); text(" kernel "); dec(k); text(" raw "); dec(b); flush();
    c0 = sceKernelGetProcessTimeCounter();
    for (int i = 0; i < N; ++i) sink += raw_rdtsc();
    c1 = sceKernelGetProcessTimeCounter();
    text("[mkw-tsc] rdtsc ps/call "); dec((c1 - c0) * 1000000ul / pf); flush();
    (void)sink;
    return 0;
}
