#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
extern "C" {
// LLVM uses the FreeBSD/POSIX int return contract, not GNU's char* variant.
// These device aliases bypass the SDK's GNU errno-numbering/string wrappers.
int __sp_device_strerror_r(int, char*, size_t);
char* __sp_device_strerror(int);
int strerror_r(int error, char* output, size_t size) {
    if (!size) return ERANGE;
    return __sp_device_strerror_r(error, output, size);
}
char* strerror(int error) { return __sp_device_strerror(error); }
// The console exports FILE objects (_Stdin/_Stdout/_Stderr), not FreeBSD's
// pointer variables. Keep FILE opaque: only pass these addresses to native
// stdio functions; never dereference FreeBSD FILE fields.
extern unsigned char _Stdin[], _Stdout[], _Stderr[];
FILE* __stdinp = reinterpret_cast<FILE*>(_Stdin);
FILE* __stdoutp = reinterpret_cast<FILE*>(_Stdout);
FILE* __stderrp = reinterpret_cast<FILE*>(_Stderr);
int scePthreadJoin(pthread_t, void**);
int scePthreadDetach(pthread_t);
int scePthreadMutexTrylock(pthread_mutex_t*);
int scePthreadMutexattrInit(pthread_mutexattr_t*);
int scePthreadMutexattrSettype(pthread_mutexattr_t*, int);
int scePthreadMutexattrDestroy(pthread_mutexattr_t*);
int scePthreadMutexInit(pthread_mutex_t*, const pthread_mutexattr_t*, const char*);
int sceKernelUsleep(unsigned);
static int PosixResult(int rc) {
    return (static_cast<unsigned>(rc) & 0xffff0000u) == 0x80020000u ? rc & 0xffff : rc;
}
int pthread_join(pthread_t thread, void** result) { return PosixResult(scePthreadJoin(thread, result)); }
int pthread_detach(pthread_t thread) { return PosixResult(scePthreadDetach(thread)); }
// FreeBSD exposes a void spelling; the verified native POSIX import returns
// an error code. Preserve the requested name and the FreeBSD return contract.
int pthread_rename_np(pthread_t, const char*);
void pthread_set_name_np(pthread_t thread, const char* name) {
    (void)pthread_rename_np(thread, name);
}
int pthread_mutex_trylock(pthread_mutex_t* mutex) { return PosixResult(scePthreadMutexTrylock(mutex)); }
// Our libc++ uses FreeBSD's opaque eight-byte attributes and kinds 1/2/3.
// SharpProspero's NativeAOT compatibility object instead emulates Linux's
// four-byte attributes and translates kinds 0/1/2. Override that ABI here:
// the console API accepts the FreeBSD kinds directly (recursive = 2).
int pthread_mutexattr_init(pthread_mutexattr_t* attr) {
    if (!attr) return EINVAL;
    return PosixResult(scePthreadMutexattrInit(attr));
}
int pthread_mutexattr_settype(pthread_mutexattr_t* attr, int kind) {
    if (!attr || !*attr || kind < PTHREAD_MUTEX_ERRORCHECK || kind > PTHREAD_MUTEX_NORMAL) return EINVAL;
    return PosixResult(scePthreadMutexattrSettype(attr, kind));
}
int pthread_mutexattr_destroy(pthread_mutexattr_t* attr) {
    if (!attr || !*attr) return EINVAL;
    return PosixResult(scePthreadMutexattrDestroy(attr));
}
int pthread_mutex_init(pthread_mutex_t* mutex, const pthread_mutexattr_t* attr) {
    if (!mutex) return EINVAL;
    return PosixResult(scePthreadMutexInit(mutex, attr, nullptr));
}
// The FreeBSD header's once object is owned by our compiled code. Do not pass
// its layout to an unverified scePthreadOnce ABI. Publish initialization with
// acquire/release ordering, and allow another attempt if the callback throws.
int pthread_once(pthread_once_t* once, void (*initialize)()) {
    for (;;) {
        int state = __atomic_load_n(&once->state, __ATOMIC_ACQUIRE);
        if (state == 2) return 0;
        if (state == 0 && __atomic_compare_exchange_n(&once->state, &state, 1, false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
            try { initialize(); }
            catch (...) { __atomic_store_n(&once->state, 0, __ATOMIC_RELEASE); throw; }
            __atomic_store_n(&once->state, 2, __ATOMIC_RELEASE);
            return 0;
        }
        sceKernelUsleep(50);
    }
}
// Module-local identity for static-object destructor registration.
void* __dso_handle = &__dso_handle;
[[noreturn]] void __assert(const char* function, const char* file, int line, const char* expression) {
    fprintf(stderr, "[mkw-cxx] assertion %s (%s:%d, %s)\n", expression, file, line, function);
    abort();
}
}
