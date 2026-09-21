// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// Fast monotonic clock for platforms where the kernel clock is expensive.
//
// On the PS5, clock_gettime(CLOCK_MONOTONIC) costs ~22 us per call (measured by
// perf_microbench_ps5.cpp), and WiiCompiled reads std::chrono::steady_clock in
// hot paths: the PowerPC time base (OSGetTime/OSGetTick and mftb), the GX
// timing service run on every GXBegin, audio and sleep-timer polls and VI
// pacing. This clock derives nanoseconds from a cheap counter and stays aligned
// with the kernel clock, so absolute deadlines handed to kernel timed waits keep
// their meaning:
//  - readers never block: a sequence lock protects the {counter, nanos} anchor;
//  - every `resyncNanos` a reader re-anchors to the slow kernel clock, which
//    bounds drift between the two time sources;
//  - returned values never decrease, across re-anchors and across threads.
// Sources are injected so the algorithm is tested on PC (tests/fast_clock.cpp).
#include <atomic>
#include <cstdint>

namespace mkw::runtime {
struct FastClockSources {
    uint64_t (*counter)();      // cheap, monotonic, `frequency` ticks per second
    uint64_t frequency;
    uint64_t (*slowNanos)();    // authoritative kernel monotonic time, nanoseconds
};

class FastMonotonicClock {
public:
    FastMonotonicClock(FastClockSources sources, uint64_t resyncNanos) noexcept
        : sources_(sources),
          resyncTicks_(ticks_for(resyncNanos, sources.frequency)) {
        const uint64_t before = sources_.counter();
        const uint64_t nanos = sources_.slowNanos();
        const uint64_t after = sources_.counter();
        baseCounter_.store(before + (after - before) / 2, std::memory_order_relaxed);
        baseNanos_.store(nanos, std::memory_order_relaxed);
        last_.store(nanos, std::memory_order_relaxed);
    }

    uint64_t now_nanos() noexcept {
        const uint64_t counter = sources_.counter();
        uint64_t baseCounter, baseNanos, sequence;
        do {
            sequence = sequence_.load(std::memory_order_acquire);
            baseCounter = baseCounter_.load(std::memory_order_acquire);
            baseNanos = baseNanos_.load(std::memory_order_acquire);
        } while ((sequence & 1) || sequence != sequence_.load(std::memory_order_acquire));

        const uint64_t elapsed = counter > baseCounter ? counter - baseCounter : 0;
        uint64_t nanos = baseNanos + scale(elapsed);
        if (elapsed >= resyncTicks_ && !resyncing_.test_and_set(std::memory_order_acquire)) {
            nanos = resync(counter);
            resyncing_.clear(std::memory_order_release);
        }
        // Never move backwards, whichever thread last observed the clock.
        uint64_t previous = last_.load(std::memory_order_acquire);
        while (nanos > previous &&
               !last_.compare_exchange_weak(previous, nanos, std::memory_order_acq_rel)) {
        }
        return nanos > previous ? nanos : previous;
    }

    uint64_t resyncs() const noexcept { return resyncs_.load(std::memory_order_relaxed); }
    uint64_t frequency() const noexcept { return sources_.frequency; }

    // Ticks-to-nanoseconds conversion, exposed for tests.
    uint64_t scale(uint64_t ticks) const noexcept {
        return uint64_t((unsigned __int128)ticks * 1000000000u / sources_.frequency);
    }

private:
    static uint64_t ticks_for(uint64_t nanos, uint64_t frequency) noexcept {
        const auto ticks = (unsigned __int128)nanos * frequency / 1000000000u;
        return ticks ? uint64_t(ticks) : 1;
    }

    uint64_t resync(uint64_t counterBefore) noexcept {
        const uint64_t real = sources_.slowNanos();
        const uint64_t counterAfter = sources_.counter();
        // Anchor the kernel reading to the middle of the slow call.
        const uint64_t anchor = counterBefore + (counterAfter - counterBefore) / 2;
        // Always re-anchor to kernel time. If the counter ran slow the clock
        // steps forward; if it ran fast, now_nanos() clamps to the last returned
        // value until kernel time catches up, so the lead never accumulates.
        const uint64_t base = real;
        const uint64_t sequence = sequence_.load(std::memory_order_acquire);
        sequence_.store(sequence + 1, std::memory_order_release);
        baseCounter_.store(anchor, std::memory_order_release);
        baseNanos_.store(base, std::memory_order_release);
        sequence_.store(sequence + 2, std::memory_order_release);
        resyncs_.fetch_add(1, std::memory_order_relaxed);
        return base + scale(counterAfter > anchor ? counterAfter - anchor : 0);
    }

    FastClockSources sources_;
    uint64_t resyncTicks_;
    std::atomic<uint64_t> sequence_{0};
    std::atomic<uint64_t> baseCounter_{0}, baseNanos_{0};
    std::atomic<uint64_t> last_{0};
    std::atomic<uint64_t> resyncs_{0};
    std::atomic_flag resyncing_ = ATOMIC_FLAG_INIT;
};
}  // namespace mkw::runtime
