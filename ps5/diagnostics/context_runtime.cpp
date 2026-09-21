#include "host_context.h"
#include "context_x86_64.h"
extern "C" void __prospero_klog(const char*);

static HostContext::Handle scheduler, worker;
static volatile unsigned steps, failures;
constexpr unsigned Rounds = 5000;
static unsigned initialMxcsr;
static unsigned short initialCw;

static unsigned mxcsr() { unsigned v; __asm__ volatile("stmxcsr %0" : "=m"(v)); return v; }
static unsigned short cw() { unsigned short v; __asm__ volatile("fnstcw %0" : "=m"(v)); return v; }
static void run(void* argument) {
    if (argument != &steps || !HostContext::IsCurrent(worker)) ++failures;
    unsigned localMxcsr = (initialMxcsr & ~0x6000u) | 0x2000u;
    unsigned short localCw = (initialCw & ~0x0c00u) | 0x0400u;
    __asm__ volatile("ldmxcsr %0; fldcw %1" : : "m"(localMxcsr), "m"(localCw));
    for (unsigned i = 0; i < Rounds; ++i) {
        ++steps;
        HostContext::Switch(scheduler);
        if (mxcsr() != localMxcsr || cw() != localCw || !HostContext::IsCurrent(worker)) ++failures;
    }
}
extern "C" int main() {
    __prospero_klog("[mkw-context] begin native HostContext test\n");
    initialMxcsr = mxcsr(); initialCw = cw();
    if (!HostContext::InitializeScheduler(&scheduler)) {
        __prospero_klog("[mkw-context] FAIL scheduler initialization\n"); return 1;
    }
    worker = HostContext::Create(65537, run, const_cast<unsigned*>(&steps));
    if (!worker) {
        __prospero_klog("[mkw-context] FAIL guarded stack allocation\n");
        HostContext::ShutdownScheduler(scheduler); return 2;
    }
    for (unsigned i = 0; i < Rounds; ++i) {
        HostContext::Switch(worker);
        if (steps != i + 1 || !HostContext::IsCurrent(scheduler) ||
            mxcsr() != initialMxcsr || cw() != initialCw) ++failures;
    }
    HostContext::Switch(worker); // finish entry and return through the trampoline
    HostContext::Switch(worker); // finished context must be ignored
    if (steps != Rounds || !HostContext::IsCurrent(scheduler)) ++failures;
    HostContext::Destroy(worker);
    HostContext::ShutdownScheduler(scheduler);
    if (HostContext::IsCurrent(scheduler)) ++failures;
    if (failures) { __prospero_klog("[mkw-context] FAIL state preservation\n"); return 3; }
    __prospero_klog("[mkw-context] PASS 5000 cycles, FP controls, guarded stack, entry return, shutdown\n");
    return 0;
}
