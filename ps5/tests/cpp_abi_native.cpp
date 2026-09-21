/* No STL: validates the native C++ ABI before building libc++ against it. */
static volatile unsigned destroyed;
struct Guard { ~Guard() { ++destroyed; } };
__attribute__((noinline)) static void raise(unsigned value) {
    Guard guard;
    throw value;
}
__attribute__((noinline)) static unsigned* allocate(unsigned value) { return new unsigned(value); }
static thread_local volatile unsigned tls = 17;
extern "C" int scePthreadCreate(void**, void*, void* (*)(void*), void*, const char*);
extern "C" int scePthreadJoin(void*, void**);
static void* worker(void* argument) {
    unsigned* status = static_cast<unsigned*>(argument);
    if (tls != 17) { *status = 1; return nullptr; }
    tls = 31;
    try { raise(0xabc); }
    catch (unsigned value) { if (value != 0xabc) *status = 2; }
    if (tls != 31) *status = 3;
    return argument;
}
extern "C" int mkw_cpp_abi_test() {
    destroyed = 0;
    unsigned* allocation = allocate(0x12345678);
    if (!allocation || *allocation != 0x12345678) return 1;
    delete allocation;
    bool caught = false;
    try { raise(0xfeed); }
    catch (unsigned value) { caught = value == 0xfeed; }
    if (!caught || destroyed != 1) return 2;
    tls = 23;
    unsigned workerStatus = 0;
    void* thread = nullptr;
    if (scePthreadCreate(&thread, nullptr, worker, &workerStatus, "mkw-cpp-abi")) return 3;
    void* returned = nullptr;
    if (scePthreadJoin(thread, &returned)) return 4;
    if (workerStatus || returned != &workerStatus || tls != 23 || destroyed != 2) return 5;
    return 0;
}
