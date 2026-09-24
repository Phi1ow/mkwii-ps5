// SPDX-License-Identifier: GPL-3.0-only
// DIAGNOSTIC micro-benchmark of the primitives the per-draw path relies on.
// Header-only and free of runtime dependencies so the identical code runs on
// the PS5 (perf_microbench_ps5.cpp, behind a flag file) and on the PC, which
// separates "slow CPU" from "slow platform service" when a stage is costly.
#pragma once
#include "cpu_cache_flush.h"
#include "hash.hpp"
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <vector>

namespace mkw::agc::microbench {
using Clock = std::chrono::steady_clock;
inline volatile uint64_t g_sink = 0;  // Keeps optimisers from deleting timed work.

// Runs body `iterations` times and prints nanoseconds per call.
template <class F>
inline double measure(std::FILE* out, const char* name, unsigned iterations, F&& body) {
    const auto start = Clock::now();
    for (unsigned i = 0; i < iterations; ++i) body(i);
    const double ns = double(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count()) / iterations;
    std::fprintf(out, "[mkw-microbench] %-34s %12.1f ns/op  (%u ops)\n", name, ns, iterations);
    return ns;
}

inline void run_core(std::FILE* out) {
    measure(out, "steady_clock::now", 200000, [](unsigned) { g_sink += uint64_t(Clock::now().time_since_epoch().count()); });
    measure(out, "malloc+free 64B", 200000, [](unsigned) { void* p = std::malloc(64); g_sink += uintptr_t(p); std::free(p); });
    measure(out, "malloc+free 4KiB", 100000, [](unsigned) { void* p = std::malloc(4096); g_sink += uintptr_t(p); std::free(p); });
    measure(out, "new+delete 64KiB", 20000, [](unsigned) { auto* p = new uint8_t[65536]; g_sink += uintptr_t(p); delete[] p; });
    measure(out, "vector<u8>(1024) zeroed", 100000, [](unsigned) { std::vector<uint8_t> v(1024); g_sink += v[512]; });
    measure(out, "make_shared<64B> + release", 100000, [](unsigned) { auto p = std::make_shared<std::array<uint8_t, 64>>(); g_sink += (*p)[0]; });
    {
        std::mutex m;
        measure(out, "mutex lock+unlock (uncontended)", 200000, [&](unsigned) { std::lock_guard lock(m); ++g_sink; });
    }
    {
        std::vector<uint8_t> src(4096, 0x5a), dst(4096);
        measure(out, "memcpy 4KiB", 100000, [&](unsigned) { std::memcpy(dst.data(), src.data(), 4096); g_sink += dst[7]; });
    }
    {
        std::vector<uint8_t> data(65536);
        for (size_t i = 0; i < data.size(); ++i) data[i] = uint8_t(i * 131);
        const double ns = measure(out, "xxh3 64KiB", 4000, [&](unsigned) { g_sink += aurora::xxh3_hash_s(data.data(), data.size()); });
        std::fprintf(out, "[mkw-microbench] %-34s %12.1f MiB/s\n", "xxh3 throughput", 65536.0 / 1048576.0 / (ns * 1e-9));
    }
    {
        std::vector<uint8_t> data(4096);
        measure(out, "clflush 4KiB (64 lines)", 20000, [&](unsigned) {
            for (size_t i = 0; i < data.size(); i += 64) __builtin_ia32_clflush(data.data() + i);
            __atomic_thread_fence(__ATOMIC_SEQ_CST);
            g_sink += data[0];
        });
        // Dirty lines, as after filling an upload arena: the old in-order loop
        // against the shared helper (CLFLUSHOPT when CPUID reports it).
        measure(out, "write+clflush 4KiB (dirty)", 20000, [&](unsigned i) {
            std::memset(data.data(), int(i), data.size());
            for (size_t j = 0; j < data.size(); j += 64) __builtin_ia32_clflush(data.data() + j);
            __atomic_thread_fence(__ATOMIC_SEQ_CST);
            g_sink += data[0];
        });
        measure(out, cpu_has_clflushopt() ? "write+clflushopt 4KiB (dirty)" : "write+clflush helper 4KiB (dirty)", 20000, [&](unsigned i) {
            std::memset(data.data(), int(i), data.size());
            flush_cpu_cache_lines(data.data(), data.size());
            g_sink += data[0];
        });
    }
}
}  // namespace mkw::agc::microbench
