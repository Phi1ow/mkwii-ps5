/* WiiCompiled HostContext implementation using the console memory API from
 * SharpProspero/Interop/Kernel/KernelMemory.cs and our SysV context primitive.
 * Compile instead of runtime/src/host_context.cpp for a native PS5 target.
 * The scheduler is confined to one host thread, as required by HostContext. */
#include "host_context.h"
#include "context_x86_64.h"

using Size = HostContext::StackSize;
extern "C" {
int sceKernelMapFlexibleMemory(void**, Size, int, int);
int sceKernelMprotect(void*, Size, int);
int sceKernelReleaseFlexibleMemory(void*, Size);
int sceKernelMunmap(void*, Size);
void* scePthreadSelf();
}
#if defined(MKW_PS5_FPROF)
// DIAGNOSTIC profiling variant (ps5/runtime/function_profiler_ps5.cpp): each
// context keeps its own profiler shadow stack across switches.
extern "C" void* mkw_fprof_fiber_leave();
extern "C" void mkw_fprof_fiber_enter(void* stack);
#endif

namespace HostContext {
namespace {
constexpr Size Page = 16384;
struct Context {
    void* savedSp;
    void* mapping;
    Size mappingSize;
    Entry entry;
    void* argument;
    bool finished;
};
static_assert(sizeof(Context) <= Page);
Context schedulerContext;
Context* current;
void* ownerThread;
Size workerCount;

bool OnSchedulerThread() {
    return ownerThread && scePthreadSelf() == ownerThread;
}

void MKW_CONTEXT_ABI Enter(void* argument) {
    auto* context = static_cast<Context*>(argument);
#if defined(MKW_PS5_FPROF)
    mkw_fprof_fiber_enter(nullptr);
#endif
    context->entry(context->argument);
    context->finished = true;
#if defined(MKW_PS5_FPROF)
    // The finished context's stack is abandoned; the scheduler restores its own.
    (void)mkw_fprof_fiber_leave();
#endif
    current = &schedulerContext;
    mkw_ps5_context_switch(&schedulerContext.savedSp, &context->savedSp);
    // Switch() excludes a finished context. Reaching here means corrupt state.
    __builtin_trap();
}

void Release(void* mapping, Size bytes) {
    // Match SharpProspero FlexibleMemoryRegion.Dispose: physical backing and
    // virtual address reservation are released separately by this API.
    sceKernelReleaseFlexibleMemory(mapping, bytes);
    sceKernelMunmap(mapping, bytes);
}
}

bool InitializeScheduler(Handle* scheduler) {
    if (!scheduler) return false;
    *scheduler = nullptr;
    if (ownerThread) return false;
    void* thread = scePthreadSelf();
    if (!thread) return false;
    ownerThread = thread;
    schedulerContext.savedSp = nullptr;
    schedulerContext.finished = false;
    current = &schedulerContext;
    *scheduler = current;
    return true;
}

void ShutdownScheduler(Handle scheduler) {
    if (!OnSchedulerThread() || scheduler != &schedulerContext ||
        current != &schedulerContext || workerCount != 0) return;
    current = nullptr;
    ownerThread = nullptr;
}

Handle Create(StackSize stackSize, Entry entry, void* argument) {
    if (!OnSchedulerThread() || !entry || !stackSize) return nullptr;
    // One guard page, rounded stack, and one page for the Context record.
    if (stackSize > static_cast<Size>(-1) - 3 * Page) return nullptr;
    Size stackBytes = (stackSize + Page - 1) & ~(Page - 1);
    Size total = stackBytes + 2 * Page;
    void* mapping = nullptr;
    if (sceKernelMapFlexibleMemory(&mapping, total, 3 /* CPU RW */, 0) != 0)
        return nullptr;
    if (sceKernelMprotect(mapping, Page, 0 /* no access */) != 0) {
        Release(mapping, total);
        return nullptr;
    }
    auto* top = static_cast<unsigned char*>(mapping) + Page + stackBytes;
    auto* context = reinterpret_cast<Context*>(top);
    context->mapping = mapping;
    context->mappingSize = total;
    context->entry = entry;
    context->argument = argument;
    context->finished = false;
    context->savedSp = mkw_ps5_context_init(top, Enter, context);
    ++workerCount;
    return context;
}

void Destroy(Handle handle) {
    if (!OnSchedulerThread() || !handle || handle == current ||
        handle == &schedulerContext) return;
    auto* context = static_cast<Context*>(handle);
    void* mapping = context->mapping;
    Size bytes = context->mappingSize;
    --workerCount;
    Release(mapping, bytes);
}

bool IsCurrent(Handle handle) {
    return OnSchedulerThread() && handle && handle == current;
}

void Switch(Handle handle) {
    if (!OnSchedulerThread() || !handle || handle == current) return;
    auto* destination = static_cast<Context*>(handle);
    if (destination->finished) return;
    Context* source = current;
    current = destination;
#if defined(MKW_PS5_FPROF)
    void* profile = mkw_fprof_fiber_leave();
#endif
    mkw_ps5_context_switch(&destination->savedSp, &source->savedSp);
#if defined(MKW_PS5_FPROF)
    mkw_fprof_fiber_enter(profile);
#endif
    current = source;
}
}
