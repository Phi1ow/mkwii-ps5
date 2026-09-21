// SPDX-License-Identifier: GPL-3.0-only
// Host checks for FastMonotonicClock with simulated counter and kernel clock:
// conversion, re-anchoring, drift in both directions, monotonicity, overflow
// at TSC-like frequencies and concurrent readers.
#include "fast_clock.h"
#include <cstdio>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using mkw::runtime::FastClockSources;
using mkw::runtime::FastMonotonicClock;
static unsigned checks;
static void check(bool v, int line = __builtin_LINE()) {
    ++checks;
    if (!v) throw std::runtime_error("fast clock check failed at line " + std::to_string(line));
}

// Simulated time: `trueNanos` is real time; the counter runs at `counterRate`
// relative to it (1.0 = perfect), the kernel clock reads true time.
static std::atomic<uint64_t> trueNanos{0};
static double counterRate = 1.0;
static uint64_t frequency = 1000000;           // 1 MHz counter
static std::atomic<unsigned> slowCalls{0};
static uint64_t counter() { return uint64_t(double(trueNanos.load()) * counterRate * double(frequency) / 1e9); }
static uint64_t kernel() { ++slowCalls; return trueNanos.load(); }

int main() { try {
    // Exact conversion, including a TSC-like 3.5 GHz counter over a day without overflow.
    {
        trueNanos = 5'000'000'000ull; frequency = 1000000; counterRate = 1.0;
        FastMonotonicClock clock({counter, frequency, kernel}, 250'000'000);
        check(clock.scale(1000000) == 1'000'000'000ull);
        check(clock.scale(1) == 1000);
        check(clock.now_nanos() == 5'000'000'000ull);
        trueNanos += 1234567000;
        check(clock.now_nanos() == 6'234'567'000ull);
    }
    {
        frequency = 3'500'000'000ull; trueNanos = 0; counterRate = 1.0;
        FastMonotonicClock clock({counter, frequency, kernel}, 250'000'000);
        const uint64_t day = 86400ull * frequency;
        check(clock.scale(day) == 86400ull * 1'000'000'000ull);
    }

    // No kernel call between re-anchors; one per interval once it elapses.
    {
        frequency = 1000000; trueNanos = 1'000'000'000ull; counterRate = 1.0;
        FastMonotonicClock clock({counter, frequency, kernel}, 250'000'000);
        const unsigned afterInit = slowCalls;
        for (int i = 0; i < 1000; ++i) { trueNanos += 100'000; (void)clock.now_nanos(); }   // 100 ms total
        check(slowCalls == afterInit && clock.resyncs() == 0);
        for (int i = 0; i < 2000; ++i) { trueNanos += 100'000; (void)clock.now_nanos(); }   // 200 ms more
        check(clock.resyncs() == 1 && slowCalls == afterInit + 1);
    }

    // Counter slower than the kernel: re-anchoring steps forward to kernel time.
    {
        frequency = 1000000; trueNanos = 0; counterRate = 0.999;   // -1000 ppm
        FastMonotonicClock clock({counter, frequency, kernel}, 250'000'000);
        uint64_t previous = 0;
        for (int i = 0; i < 100000; ++i) {
            trueNanos += 100'000;
            const uint64_t now = clock.now_nanos();
            check(now >= previous);
            previous = now;
        }
        // 10 s simulated: without re-anchoring the error would reach 10 ms.
        const int64_t error = int64_t(clock.now_nanos()) - int64_t(trueNanos.load());
        check(error > -300'000 && error < 300'000);   // bounded by one interval x 1000 ppm plus a step
        check(clock.resyncs() >= 39);
    }

    // Counter faster than the kernel: time holds rather than stepping back.
    {
        frequency = 1000000; trueNanos = 0; counterRate = 1.001;   // +1000 ppm
        FastMonotonicClock clock({counter, frequency, kernel}, 250'000'000);
        uint64_t previous = 0;
        for (int i = 0; i < 100000; ++i) {
            trueNanos += 100'000;
            const uint64_t now = clock.now_nanos();
            check(now >= previous);
            previous = now;
        }
        const int64_t error = int64_t(clock.now_nanos()) - int64_t(trueNanos.load());
        check(error > -300'000 && error < 300'000);
    }

    // Concurrent readers never observe time going backwards.
    {
        frequency = 1000000; trueNanos = 0; counterRate = 1.0005;
        FastMonotonicClock clock({counter, frequency, kernel}, 1'000'000);   // re-anchor every 1 ms
        std::atomic<bool> stop{false};
        std::thread writer([&] { while (!stop) { trueNanos += 1000; std::this_thread::yield(); } });
        std::vector<std::thread> readers;
        std::atomic<unsigned> violations{0};
        for (int t = 0; t < 4; ++t) readers.emplace_back([&] {
            uint64_t previous = 0;
            for (int i = 0; i < 200000; ++i) { const uint64_t now = clock.now_nanos(); if (now < previous) ++violations; previous = now; }
        });
        for (auto& r : readers) r.join();
        stop = true; writer.join();
        check(violations == 0);
        check(clock.resyncs() > 0);
    }

    std::printf("PASS fast monotonic clock: %u checks, conversion, re-anchoring, drift both ways, monotonic and concurrent\n", checks);
    return 0;
} catch (const std::exception& e) { std::fprintf(stderr, "FAIL after %u: %s\n", checks, e.what()); return 1; } }
