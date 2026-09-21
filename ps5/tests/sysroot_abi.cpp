#include <stddef.h>
#include <pthread.h>
#include <sys/stat.h>
#include <time.h>
// Selected ABI contracts from SharpProspero's KernelFile, KernelTimespec and
// Threading bindings. The stat offsets were also measured by the native probe.
// This does not validate FILE internals, locale data or every libc interface.
static_assert(sizeof(void*) == 8 && sizeof(long) == 8, "native LP64");
static_assert(offsetof(struct stat, st_mode) == 8, "native stat mode");
static_assert(offsetof(struct stat, st_uid) == 12, "native stat uid");
static_assert(offsetof(struct stat, st_size) == 72, "native stat size");
static_assert(sizeof(struct timespec) == 16, "native timespec");
static_assert(sizeof(pthread_t) == 8 && sizeof(pthread_mutex_t) == 8 &&
              sizeof(pthread_cond_t) == 8, "native opaque pthread handles");
