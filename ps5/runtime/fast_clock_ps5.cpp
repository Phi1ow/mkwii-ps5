// SPDX-License-Identifier: GPL-3.0-only
// PS5 clock_gettime override for the game executable.
//
// The kernel clock costs ~22 us per read on this console while WiiCompiled,
// Aurora and libc++ read std::chrono::steady_clock in hot paths. Defining
// clock_gettime in the game link routes those reads through
// FastMonotonicClock (fast_clock.h): the monotonic clock ids are computed from
// the per-process time counter and re-anchored to the kernel clock every
// 250 ms; every other clock id is forwarded to the kernel unchanged. Code inside
// system modules keeps using the kernel symbol and is unaffected.
#include "fast_clock.h"
#include <cerrno>
#include <ctime>

extern "C" {
int sceKernelClockGettime(int, struct timespec*);
uint64_t sceKernelGetProcessTimeCounter();
uint64_t sceKernelGetProcessTimeCounterFrequency();
}

namespace {
// FreeBSD clock ids (sys/_clock_id.h).
constexpr clockid_t kMonotonic = 4, kMonotonicPrecise = 11, kMonotonicFast = 12;
constexpr uint64_t kResyncNanos = 250'000'000;

int kernel_clock(clockid_t id, struct timespec* ts) {
    const int rc = sceKernelClockGettime(int(id), ts);
    if (rc < 0) {
        // SCE kernel errors carry the POSIX errno in the low 16 bits.
        errno = rc & 0xffff;
        return -1;
    }
    return 0;
}

uint64_t kernel_monotonic_nanos() {
    struct timespec ts{};
    if (kernel_clock(kMonotonic, &ts) != 0) return 0;
    return uint64_t(ts.tv_sec) * 1000000000u + uint64_t(ts.tv_nsec);
}

mkw::runtime::FastMonotonicClock* fast_clock() {
    static const uint64_t frequency = sceKernelGetProcessTimeCounterFrequency();
    if (!frequency) return nullptr;  // No usable counter: stay on the kernel clock.
    static mkw::runtime::FastMonotonicClock clock(
        {sceKernelGetProcessTimeCounter, frequency, kernel_monotonic_nanos}, kResyncNanos);
    return &clock;
}
}  // namespace

extern "C" uint64_t mkw_fast_clock_resyncs() {
    const auto* clock = fast_clock();
    return clock ? clock->resyncs() : 0;
}

extern "C" int clock_gettime(clockid_t id, struct timespec* ts) {
    if (!ts) {
        errno = EFAULT;
        return -1;
    }
    if (id == kMonotonic || id == kMonotonicPrecise || id == kMonotonicFast) {
        if (auto* clock = fast_clock()) {
            const uint64_t nanos = clock->now_nanos();
            ts->tv_sec = time_t(nanos / 1000000000u);
            ts->tv_nsec = long(nanos % 1000000000u);
            return 0;
        }
    }
    return kernel_clock(id, ts);
}
