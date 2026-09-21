// SPDX-License-Identifier: GPL-3.0-only
// DIAGNOSTIC sampling profiler, started at Aurora start-up only when
// /app0/UserData/profile.txt exists.
//
// Guest threads run as fibers that share one live CpuContext: a switch saves
// the outgoing thread's registers and restores the incoming thread's into the
// same object (fiber_manager.cpp). Translated code stores the return address in
// ctx->lr before every call, so sampling that register from a separate host
// thread shows which guest call site is executing, including time spent in
// native HLE/Aurora services that the site called. Samples are grouped by the
// guest function containing the address, named from the generated symbol map
// the crash reporter already uses. The sampler never writes guest state.
#include "abi_bridge.h"
#include "hle_stubs.h"
#include "async_log.h"
#include "gx_perf_stats.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <iterator>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

// Weak: GPU diagnostics link this archive without the translated game, which
// alone defines the symbol map and the persistent context.
extern "C" {
extern const uint32_t kGuestMapSymbolCount __attribute__((weak));
extern const uint32_t kGuestMapSymbolAddresses[] __attribute__((weak));
extern const char* const kGuestMapSymbolNames[] __attribute__((weak));
}
CpuContext& GetPersistentCpuContext() __attribute__((weak));
extern "C" void mkw_report_thread_placement(const char* who);
extern "C" {
uint64_t sceKernelGetProcessTimeCounter();
uint64_t sceKernelGetProcessTimeCounterFrequency();
}

namespace {
constexpr uint32_t kNoSymbol = 0xFFFFFFFFu;
// Low key bits: guest symbol index, or a native site when kNativeFlag is set.
constexpr uint32_t kNativeFlag = 0x80000000u;
constexpr uint32_t kGatherPipe = 0x7FFFFFFEu;  // native site 0xCC008000
constexpr uint32_t kNativeUnknown = 0x7FFFFFFDu;  // native site outside the symbol map
constexpr uint32_t kGatherBurst = 0x7FFFFFFCu;  // native site 0xCC008001
constexpr auto kPeriod = std::chrono::milliseconds(1);
constexpr auto kReport = std::chrono::seconds(20);
constexpr size_t kTopFunctions = 30;
std::atomic<bool> g_started{false};
// A 1 ms sleep that returns this late means the sampler thread itself was not
// scheduled: the stall is process-wide, not a wait inside the main thread.
constexpr uint64_t kLateWakeNanos = 250'000'000;

// FreeBSD CLOCK_PROCESS_CPUTIME_ID / CLOCK_THREAD_CPUTIME_ID (time.h), forwarded
// to the kernel by fast_clock_ps5.cpp. Returns -1 when the kernel refuses the id.
int64_t cpu_nanos(clockid_t id) {
    struct timespec ts{};
    if (clock_gettime(id, &ts) != 0) return -1;
    return int64_t(ts.tv_sec) * 1000000000 + ts.tv_nsec;
}

// Same floor lookup and 64 KiB gap cap as system_bridge.cpp GuestMapSymbolFloor.
uint32_t symbol_floor(uint32_t address) {
    if (kGuestMapSymbolCount == 0) return kNoSymbol;
    uint32_t lo = 0, hi = kGuestMapSymbolCount;
    while (lo < hi) {
        const uint32_t mid = lo + (hi - lo) / 2;
        if (kGuestMapSymbolAddresses[mid] <= address) lo = mid + 1; else hi = mid;
    }
    if (lo == 0 || address - kGuestMapSymbolAddresses[lo - 1] >= 0x10000u) return kNoSymbol;
    return lo - 1;
}

void report(std::unordered_map<uint64_t, uint32_t>& counts, uint64_t samples, uint64_t zero) {
    std::vector<std::pair<uint64_t, uint32_t>> ranked(counts.begin(), counts.end());
    std::sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
    mkw_log("[mkw-profile] %llu samples (lr=0: %llu), top guest call-site functions:\n",
                 (unsigned long long)samples, (unsigned long long)zero);
    for (size_t i = 0; i < ranked.size() && i < kTopFunctions; ++i) {
        const auto [key, count] = ranked[i];
        const uint32_t phase = key >> 32, low = uint32_t(key);
        const char* phaseName = phase < std::size(mkw::agc::kHostPhaseNames) ? mkw::agc::kHostPhaseNames[phase] : "?";
        const double share = samples ? 100.0 * count / double(samples) : 0.0;
        const bool native = low != kNoSymbol && (low & kNativeFlag);
        const uint32_t index = native ? low & ~kNativeFlag : low;
        if (native && index == kGatherPipe) {
            mkw_log("[mkw-profile] %6.2f%% %7u [%s] native gather-pipe FIFO write\n", share, count, phaseName);
        } else if (native && index == kGatherBurst) {
            mkw_log("[mkw-profile] %6.2f%% %7u [%s] native gather-pipe FIFO burst\n", share, count, phaseName);
        } else if (native && index == kNativeUnknown) {
            mkw_log("[mkw-profile] %6.2f%% %7u [%s] native <no guest symbol>\n", share, count, phaseName);
        } else if (native) {
            mkw_log("[mkw-profile] %6.2f%% %7u [%s] native %s (0x%08x)\n", share, count, phaseName,
                    kGuestMapSymbolNames[index], kGuestMapSymbolAddresses[index]);
        } else if (index == kNoSymbol) {
            mkw_log("[mkw-profile] %6.2f%% %7u [%s] <no guest symbol>\n", share, count, phaseName);
        } else {
            mkw_log("[mkw-profile] %6.2f%% %7u [%s] %s (0x%08x)\n", share, count, phaseName,
                         kGuestMapSymbolNames[index], kGuestMapSymbolAddresses[index]);
        }
    }
    counts.clear();
}

void sample_loop() {
    const CpuContext& live = GetPersistentCpuContext();
    std::unordered_map<uint64_t, uint32_t> counts;  // key: host phase << 32 | guest symbol index
    counts.reserve(4096);
    uint64_t samples = 0, zero = 0;
    auto nextReport = std::chrono::steady_clock::now() + kReport;
    const uint64_t frequency = sceKernelGetProcessTimeCounterFrequency();
    for (;;) {
        // Read the counter directly so a late wake is measured independently of
        // the clock override. Process CPU time tells a spinning process (CPU
        // grows with wall time) from a blocked or descheduled one (it does not).
        const uint64_t before = sceKernelGetProcessTimeCounter();
        const int64_t cpuBefore = cpu_nanos(15);
        const uint32_t lrBefore = live.lr;
        const uint32_t phaseBefore = mkw::agc::g_hostPhase.load(std::memory_order_relaxed);
        std::this_thread::sleep_for(kPeriod);
        const uint64_t elapsed = frequency ? uint64_t((unsigned __int128)(sceKernelGetProcessTimeCounter() - before) *
                                                      1000000000u / frequency) : 0;
        if (elapsed >= kLateWakeNanos) {
            const int64_t cpuAfter = cpu_nanos(15);
            const uint32_t lrAfter = live.lr;
            const uint32_t phaseAfter = mkw::agc::g_hostPhase.load(std::memory_order_relaxed);
            const uint32_t symBefore = symbol_floor(lrBefore), symAfter = symbol_floor(lrAfter);
            auto name = [](uint32_t sym) { return sym == kNoSymbol ? "<none>" : kGuestMapSymbolNames[sym]; };
            auto phaseName = [](uint32_t phase) {
                return phase < std::size(mkw::agc::kHostPhaseNames) ? mkw::agc::kHostPhaseNames[phase] : "?";
            };
            mkw_log(
                "[mkw-watchdog] sampler 1 ms sleep returned after %.0f ms; process cpu %+.0f ms (%s); "
                "before [%s] %s lr=0x%08x; after [%s] %s lr=0x%08x\n",
                elapsed / 1e6, cpuBefore >= 0 && cpuAfter >= 0 ? (cpuAfter - cpuBefore) / 1e6 : 0.0,
                cpuBefore >= 0 && cpuAfter >= 0 ? "measured" : "unavailable",
                phaseName(phaseBefore), name(symBefore), lrBefore, phaseName(phaseAfter), name(symAfter), lrAfter);
        }
        const uint32_t lr = live.lr;  // Single aligned 32-bit load; a torn value is impossible on x86.
        ++samples;
        if (lr == 0) { ++zero; continue; }
        const uint64_t phase = mkw::agc::g_hostPhase.load(std::memory_order_relaxed);
        // A running native replacement owns the sample; lr then names its guest caller only.
        const uint32_t site = g_mkwNativeSite.load(std::memory_order_relaxed);
        uint32_t low = symbol_floor(lr);
        if (site == 0xCC008000u) low = kNativeFlag | kGatherPipe;
        else if (site == 0xCC008001u) low = kNativeFlag | kGatherBurst;
        else if (site) {
            const uint32_t symbol = symbol_floor(site);
            low = kNativeFlag | (symbol == kNoSymbol ? kNativeUnknown : symbol);
        }
        ++counts[(phase << 32) | low];
        if (std::chrono::steady_clock::now() >= nextReport) {
            report(counts, samples, zero);
            samples = zero = 0;
            nextReport = std::chrono::steady_clock::now() + kReport;  // no catch-up reports after a slow one
        }
    }
}
}  // namespace

extern "C" void mkw_start_guest_sampler_if_requested() {
    if (std::FILE* flag = std::fopen("/app0/UserData/profile.txt", "rb")) {
        std::fclose(flag);
    } else {
        return;
    }
    if (!&GetPersistentCpuContext || !&kGuestMapSymbolCount || !kGuestMapSymbolAddresses || !kGuestMapSymbolNames) {
        std::fputs("[mkw-profile] no translated game linked; sampler not started\n", stderr);
        return;
    }
    if (g_started.exchange(true)) return;
    mkw_log("[mkw-profile] DIAGNOSTIC guest call-site sampler started (%u symbols, 1 ms period)\n",
                 kGuestMapSymbolCount);
    std::thread([]{mkw_report_thread_placement("sampler thread");sample_loop();}).detach();
}
