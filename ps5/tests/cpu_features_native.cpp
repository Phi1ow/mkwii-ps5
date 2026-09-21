#include <cpuid.h>
#include <cstdint>
extern "C" void mkw_diagnostic_log(const char*);
extern "C" int mkw_ppc_vectors_test();
// Baseline-only diagnostic of the feature set listed by WiiCompiled's
// host_cpu_baseline.cpp. Never execute XGETBV before checking OSXSAVE.
extern "C" int mkw_cpu_features_test() {
    unsigned a, b, c, d;
    if (__get_cpuid_max(0, nullptr) < 7 || __get_cpuid_max(0x80000000, nullptr) < 0x80000001) return 1;
    __cpuid_count(1, 0, a, b, c, d);
    constexpr unsigned ecx = (1u<<0)|(1u<<9)|(1u<<12)|(1u<<13)|(1u<<19)|(1u<<20)|(1u<<22)|(1u<<23)|(1u<<27)|(1u<<28)|(1u<<29);
    if ((c & ecx) != ecx) return 2;
    __cpuid_count(7, 0, a, b, c, d);
    constexpr unsigned ebx = (1u<<3)|(1u<<5)|(1u<<8);
    if ((b & ebx) != ebx) return 3;
    __cpuid_count(0x80000001, 0, a, b, c, d);
    if ((c & 0x21u) != 0x21u) return 4;
    __asm__ volatile("xgetbv" : "=a"(a), "=d"(d) : "c"(0));
    if ((a & 6u) != 6u) return 5;
    mkw_diagnostic_log("[mkw-cpu] PASS x86-64-v3 CPUID and XCR0 XMM/YMM state\n");
    return mkw_ppc_vectors_test();
}
