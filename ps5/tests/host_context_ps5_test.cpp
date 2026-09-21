#include "host_context.h"
#include <windows.h>
#include <cstdio>
#include <cstdint>
#include <cstdlib>

using Size = HostContext::StackSize;
static unsigned liveMappings, releases, unmaps;
static bool failMap, failProtect;
static void* lastMapping;
static Size lastSize;
static int errors, steps;
static HostContext::Handle scheduler, worker;
static void check(bool ok, const char* label) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", label); ++errors; }
}

// Kernel-memory adapter for host execution of the actual PS5 implementation.
// Real guard pages are used; only API plumbing and fault injection are replaced.
extern "C" void* scePthreadSelf() {
    return reinterpret_cast<void*>(static_cast<uintptr_t>(GetCurrentThreadId()));
}
extern "C" int sceKernelMapFlexibleMemory(void** out, Size bytes, int prot, int flags) {
    check(prot == 3 && flags == 0 && bytes % 16384 == 0, "PS5 allocation contract");
    if (failMap) return -1;
    *out = VirtualAlloc(nullptr, bytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!*out) return -1;
    lastMapping = *out; lastSize = bytes; ++liveMappings;
    return 0;
}
extern "C" int sceKernelMprotect(void* address, Size bytes, int prot) {
    check(address == lastMapping && bytes == 16384 && prot == 0, "16 KiB stack guard");
    if (failProtect) return -1;
    DWORD previous;
    return VirtualProtect(address, bytes, PAGE_NOACCESS, &previous) ? 0 : -1;
}
extern "C" int sceKernelReleaseFlexibleMemory(void* address, Size bytes) {
    ++releases;
    return VirtualFree(address, bytes, MEM_DECOMMIT) ? 0 : -1;
}
extern "C" int sceKernelMunmap(void* address, Size) {
    ++unmaps; --liveMappings;
    return VirtualFree(address, 0, MEM_RELEASE) ? 0 : -1;
}
static void run_worker(void* arg) {
    check(arg == &steps && HostContext::IsCurrent(worker), "worker identity and argument");
    ++steps;
    HostContext::Switch(scheduler);
    check(HostContext::IsCurrent(worker), "resumed worker identity");
    ++steps;
    // Returning from an entry yields to the scheduler and makes it unresumable.
}
static DWORD WINAPI foreign_thread(void*) {
    check(!HostContext::IsCurrent(scheduler), "foreign thread identity rejected");
    check(!HostContext::Create(16384, run_worker, nullptr), "foreign thread allocation rejected");
    HostContext::Switch(worker);
    HostContext::Destroy(worker);
    HostContext::ShutdownScheduler(scheduler);
    return 0;
}
int main() {
    check(!HostContext::InitializeScheduler(nullptr), "null scheduler rejected");
    check(!HostContext::Create(16384, run_worker, nullptr), "allocation before initialization rejected");
    check(HostContext::InitializeScheduler(&scheduler), "scheduler initialized");
    HostContext::Handle duplicate;
    check(!HostContext::InitializeScheduler(&duplicate), "duplicate scheduler rejected");
    check(!HostContext::Create(0, run_worker, nullptr), "zero stack rejected");
    check(!HostContext::Create(static_cast<Size>(-1), run_worker, nullptr), "stack size overflow rejected");
    check(!HostContext::Create(16384, nullptr, nullptr), "null entry rejected");
    failMap = true;
    check(!HostContext::Create(16384, run_worker, nullptr), "map failure propagated");
    failMap = false; failProtect = true;
    check(!HostContext::Create(16384, run_worker, nullptr), "guard failure propagated");
    check(liveMappings == 0 && releases == 1 && unmaps == 1, "guard failure releases mapping");
    failProtect = false;
    worker = HostContext::Create(65537, run_worker, &steps);
    check(worker != nullptr && lastSize == 7 * 16384, "stack rounded with guard and metadata");
    MEMORY_BASIC_INFORMATION info{};
    VirtualQuery(lastMapping, &info, sizeof(info));
    check(info.Protect == PAGE_NOACCESS && info.RegionSize == 16384, "guard page actually inaccessible");
    HANDLE thread = CreateThread(nullptr, 0, foreign_thread, nullptr, 0, nullptr);
    check(thread != nullptr, "foreign test thread created");
    if (thread) { WaitForSingleObject(thread, INFINITE); CloseHandle(thread); }
    check(liveMappings == 1, "foreign thread did not destroy stack");
    HostContext::ShutdownScheduler(scheduler);
    check(HostContext::IsCurrent(scheduler), "shutdown refused while worker is alive");
    HostContext::Switch(worker);
    check(steps == 1 && HostContext::IsCurrent(scheduler), "first yield");
    HostContext::Switch(worker);
    check(steps == 2 && HostContext::IsCurrent(scheduler), "entry return yields to scheduler");
    HostContext::Switch(worker);
    check(steps == 2, "finished worker cannot resume");
    HostContext::Destroy(worker);
    check(liveMappings == 0 && releases == 2 && unmaps == 2, "normal destroy releases mapping");
    HostContext::ShutdownScheduler(scheduler);
    check(!HostContext::IsCurrent(scheduler), "scheduler shutdown");
    check(HostContext::InitializeScheduler(&scheduler), "scheduler can reinitialize");
    HostContext::ShutdownScheduler(scheduler);
    std::printf("PS5 HostContext host checks: %d errors\n", errors);
    return errors != 0;
}
