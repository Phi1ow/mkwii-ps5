// SPDX-License-Identifier: GPL-3.0-only
// DIAGNOSTIC exact function profiler for the translated game code.
//
// Linked only into the profiling variant of the game (Compile-Game.ps1
// -FunctionProfile), whose translated objects are compiled with
// -finstrument-functions-after-inlining: every translated function that
// survives inlining calls __cyg_profile_func_enter/exit, and native services
// called through abi_bridge.h report themselves with mkw_fprof_native_*.
//
// Each hook reads the time-stamp counter on entry (t0) and on return (t1).
// The interval since the previous hook is charged to the function on top of
// the current shadow stack (self time), so nested calls are never counted
// twice; the hook's own [t0, t1] is excluded from every self and inclusive
// time. Inclusive time is credited only to the outermost activation of a
// function, so recursion is not counted twice either.
//
// Guest threads are fibers on one host thread (fiber_manager.cpp). Each fiber
// owns a shadow stack: HostContext::Switch hands the outgoing stack to
// mkw_fprof_fiber_leave and restores it after the switch returns, and time a
// fiber spends switched out is excluded from its open inclusive times.
//
// Reports are produced on the guest thread at frame end, every kWindow frames:
// [mkw-fprof] lines in stderr.log and a full CSV in UserData/Logs.
#include "abi_bridge.h"
#include "async_log.h"
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <vector>

extern "C" {
extern const uint32_t kGuestMapSymbolCount __attribute__((weak));
extern const uint32_t kGuestMapSymbolAddresses[] __attribute__((weak));
extern const char* const kGuestMapSymbolNames[] __attribute__((weak));
uint64_t sceKernelGetTscFrequency();
uint64_t sceKernelGetProcessTime();
void __cyg_profile_func_enter(void* function, void* caller);
void __cyg_profile_func_exit(void* function, void* caller);
void mkw_fprof_native_enter(uint32_t site);
void mkw_fprof_native_exit(uint32_t site);
void* mkw_fprof_fiber_leave();
void mkw_fprof_fiber_enter(void* stack);
void mkw_fprof_frame_end();
}

// Cold scalar access counters of the profiling variant's memory.cpp (weak: the
// variant links them, a plain game never calls into this file).
struct MkwSlowAccessCounters {
    uint64_t calls[13][2];
    uint64_t sampledTicks[13];
    uint64_t sampled[13];
    uint32_t pages[4096];
};
extern "C" MkwSlowAccessCounters* mkw_fprof_memory_counters() __attribute__((weak));

namespace {
constexpr unsigned kTableBits = 16;
constexpr size_t kTableSize = size_t(1) << kTableBits;
constexpr uint32_t kMaxDepth = 2048;
constexpr unsigned kWindow = 600;        // frames per report
constexpr unsigned kWarmupFrames = 60;   // frames before the first window starts
constexpr uintptr_t kNativeBit = uintptr_t(1) << 63;
constexpr size_t kTopSelf = 60, kTopInclusive = 30;

struct Stat {
    uintptr_t key;
    uint64_t calls, childCalls, self, inclusive;
    uint32_t active, pad;
};
struct Frame {
    Stat* stat;
    uint64_t enter, excludedAtEnter;
    bool outermost;
};
struct Stack {
    uint64_t excluded = 0;   // hook time plus time switched out, since creation
    uint64_t leftAt = 0;
    uint32_t depth = 0, overflow = 0;
    Frame frames[kMaxDepth];
};
struct ThreadState {
    std::unique_ptr<Stat[]> table{new Stat[kTableSize]()};
    Stat full{};  // shared by keys that no longer fit the table
    Stack* stack = nullptr;
    uint64_t last = 0;
    uint64_t root = 0, hook = 0, events = 0, unmatched = 0, unwound = 0, overflows = 0, switches = 0;
};

bool g_enabled = false;
thread_local ThreadState* t_state = nullptr;
ThreadState* g_owner = nullptr;

__attribute__((always_inline)) inline uint64_t tsc() { return __builtin_ia32_rdtsc(); }

Stack* new_stack() { return new Stack(); }  // a finished fiber's stack is not reused

ThreadState* state() {
    if (__builtin_expect(t_state != nullptr, 1)) return t_state;
    auto* s = new ThreadState();
    s->stack = new_stack();
    s->last = tsc();
    t_state = s;
    return s;
}

Stat* find(ThreadState* s, uintptr_t key) {
    const size_t mask = kTableSize - 1;
    size_t i = size_t((uint64_t(key) * 0x9E3779B97F4A7C15ull) >> (64 - kTableBits));
    for (size_t n = 0; n < kTableSize; ++n, i = (i + 1) & mask) {
        Stat& e = s->table[i];
        if (e.key == key) return &e;
        if (e.key == 0) { e.key = key; return &e; }
    }
    return &s->full;
}

__attribute__((always_inline)) inline void charge(ThreadState* s, uint64_t now) {
    const uint64_t delta = now - s->last;
    Stack* k = s->stack;
    if (k->depth) k->frames[k->depth - 1].stat->self += delta; else s->root += delta;
}

__attribute__((always_inline)) inline void push(ThreadState* s, uintptr_t key, uint64_t t0) {
    charge(s, t0);
    Stack* k = s->stack;
    Stat* stat = find(s, key);
    ++stat->calls;
    if (k->depth) ++k->frames[k->depth - 1].stat->childCalls;
    if (k->depth < kMaxDepth) {
        Frame& f = k->frames[k->depth++];
        f.stat = stat; f.enter = t0; f.excludedAtEnter = k->excluded;
        f.outermost = stat->active++ == 0;
    } else {
        ++k->overflow; ++s->overflows;
    }
}

void finalize(Stack* k, uint64_t now) {
    Frame& f = k->frames[--k->depth];
    --f.stat->active;
    if (f.outermost) f.stat->inclusive += (now - f.enter) - (k->excluded - f.excludedAtEnter);
}

__attribute__((always_inline)) inline void pop(ThreadState* s, uintptr_t key, uint64_t t0) {
    charge(s, t0);
    Stack* k = s->stack;
    if (k->overflow) { --k->overflow; return; }
    if (k->depth && k->frames[k->depth - 1].stat->key == key) { finalize(k, t0); return; }
    // An exit without its entry (a frame opened before profiling started, or
    // abandoned by an exception): unwind to the matching entry if one exists.
    for (uint32_t i = k->depth; i-- > 0;) {
        if (k->frames[i].stat->key != key) continue;
        while (k->depth > i) { finalize(k, t0); ++s->unwound; }
        return;
    }
    ++s->unmatched;
}

__attribute__((always_inline)) inline void finish_hook(ThreadState* s, uint64_t t0) {
    const uint64_t t1 = tsc();
    s->stack->excluded += t1 - t0;
    s->hook += t1 - t0;
    ++s->events;
    s->last = t1;
}

uint32_t symbol_floor(uint32_t address) {
    if (!&kGuestMapSymbolCount || kGuestMapSymbolCount == 0) return UINT32_MAX;
    uint32_t lo = 0, hi = kGuestMapSymbolCount;
    while (lo < hi) {
        const uint32_t mid = lo + (hi - lo) / 2;
        if (kGuestMapSymbolAddresses[mid] <= address) lo = mid + 1; else hi = mid;
    }
    if (lo == 0 || address - kGuestMapSymbolAddresses[lo - 1] >= 0x10000u) return UINT32_MAX;
    return lo - 1;
}

// Guest address and readable name of a profiled key.
struct Named { uint32_t guest; std::string name; };
Named describe(uintptr_t key) {
    char buf[256];
    if (key & kNativeBit) {
        const uint32_t site = uint32_t(key);
        const uint32_t sym = symbol_floor(site);
        std::snprintf(buf, sizeof buf, "native %s", sym != UINT32_MAX && kGuestMapSymbolAddresses[sym] == site
            ? kGuestMapSymbolNames[sym] : "<unnamed>");
        return {site, buf};
    }
    const auto info = TranslatedFunctionRegistry::FindByHostAddress(key);
    if (info && (reinterpret_cast<uintptr_t>(info->entryPoint) == key ||
                 reinterpret_cast<uintptr_t>(info->rawCpuInvoker) == key)) {
        const uint32_t sym = symbol_floor(info->address);
        if (sym != UINT32_MAX && kGuestMapSymbolAddresses[sym] == info->address)
            std::snprintf(buf, sizeof buf, "%s", kGuestMapSymbolNames[sym]);
        else if (sym != UINT32_MAX)
            std::snprintf(buf, sizeof buf, "%s+0x%x", kGuestMapSymbolNames[sym], info->address - kGuestMapSymbolAddresses[sym]);
        else
            std::snprintf(buf, sizeof buf, "%s", info->name && *info->name ? info->name : "<translated>");
        return {info->address, buf};
    }
    // Not a registered entry point (an out-of-line helper emitted in a
    // translated object): name it by its offset from this report function,
    // resolved offline with game.elf.functions.json.
    std::snprintf(buf, sizeof buf, "host anchor%+lld",
                  (long long)(int64_t(key) - int64_t(reinterpret_cast<uintptr_t>(&mkw_fprof_frame_end))));
    return {0, buf};
}

// Residual hook cost that lands inside charged intervals (the call into a hook
// before t0 and the return after t1), per call, measured with a private state.
double g_biasTicksPerCall = 0;
__attribute__((noinline)) void calibration_leaf() {
    __cyg_profile_func_enter(reinterpret_cast<void*>(&calibration_leaf), nullptr);
    __cyg_profile_func_exit(reinterpret_cast<void*>(&calibration_leaf), nullptr);
}
void calibrate() {
    ThreadState* saved = t_state;
    const bool wasEnabled = g_enabled;
    g_enabled = true;
    auto* probe = new ThreadState();
    probe->stack = new Stack();
    t_state = probe;
    const uint64_t outer = 0x1;
    probe->last = tsc();
    __cyg_profile_func_enter(reinterpret_cast<void*>(outer), nullptr);
    constexpr unsigned kCalls = 200000;
    for (unsigned i = 0; i < kCalls; ++i) calibration_leaf();
    __cyg_profile_func_exit(reinterpret_cast<void*>(outer), nullptr);
    Stat* leaf = find(probe, reinterpret_cast<uintptr_t>(&calibration_leaf));
    Stat* root = find(probe, outer);
    // The leaf body is empty: its self time is the callee-side residual. The
    // caller-side residual is of the same order; both are reported.
    g_biasTicksPerCall = leaf->calls ? double(leaf->self) / double(leaf->calls) : 0;
    const double hookPerEvent = probe->events ? double(probe->hook) / double(probe->events) : 0;
    const double freq = double(sceKernelGetTscFrequency());
    mkw_log("[mkw-fprof] calibration: %u empty calls; hook body %.2f ns/event (excluded), residual callee %.2f ns/call, "
            "caller loop+residual %.2f ns/call (charged)\n",
            kCalls, hookPerEvent * 1e9 / freq, g_biasTicksPerCall * 1e9 / freq,
            root->calls ? double(root->self) / kCalls * 1e9 / freq : 0.0);
    t_state = saved;
    g_enabled = wasEnabled;
    delete probe->stack;
    delete probe;
}

struct Window {
    uint64_t startTsc = 0, startRoot = 0, startHook = 0, startEvents = 0;
    uint64_t startUnmatched = 0, startUnwound = 0, startOverflows = 0, startSwitches = 0;
    unsigned frames = 0, index = 0;
    bool started = false;
} g_window;
unsigned g_framesSeen = 0;
std::unique_ptr<MkwSlowAccessCounters> g_slowAtWindow{new MkwSlowAccessCounters()};

void begin_window(ThreadState* s) {
    for (size_t i = 0; i < kTableSize; ++i) {
        Stat& e = s->table[i];
        e.calls = e.childCalls = e.self = e.inclusive = 0;
    }
    g_window.startTsc = tsc();
    g_window.startRoot = s->root; g_window.startHook = s->hook; g_window.startEvents = s->events;
    g_window.startUnmatched = s->unmatched; g_window.startUnwound = s->unwound;
    g_window.startOverflows = s->overflows; g_window.startSwitches = s->switches;
    g_window.frames = 0;
    g_window.started = true;
    if (mkw_fprof_memory_counters) std::memcpy(g_slowAtWindow.get(), mkw_fprof_memory_counters(), sizeof(MkwSlowAccessCounters));
}

// Cold scalar accesses of the window: kind, calls per frame on pages without /
// with a pending deferred read, sampled cost per call, and the busiest pages.
void report_slow_accesses(double frames, double freq) {
    if (!mkw_fprof_memory_counters) return;
    const MkwSlowAccessCounters& now = *mkw_fprof_memory_counters();
    const MkwSlowAccessCounters& then = *g_slowAtWindow;
    static const char* const kNames[13] = {"r8", "r16", "r32", "r64", "rf32", "rf64", "w8", "w16", "w32", "w64",
                                           "wf32", "wf64", "fifo"};
    char line[1600];
    int n = std::snprintf(line, sizeof line, "[mkw-fprof] cold accesses per frame (plain page / deferred page, ns per call):");
    double totalMs = 0;
    for (unsigned k = 0; k < 13; ++k) {
        const double plain = double(now.calls[k][0] - then.calls[k][0]) / frames;
        const double covered = double(now.calls[k][1] - then.calls[k][1]) / frames;
        const uint64_t sampled = now.sampled[k] - then.sampled[k];
        const double ns = sampled ? double(now.sampledTicks[k] - then.sampledTicks[k]) * 1e9 / freq / double(sampled) : 0.0;
        if (plain + covered < 0.05) continue;
        totalMs += (plain + covered) * ns * 1e-6;
        if (n > 0 && n < int(sizeof line))
            n += std::snprintf(line + n, sizeof line - size_t(n), " %s %.0f/%.0f %.0fns;", kNames[k], plain, covered, ns);
    }
    if (n > 0 && n < int(sizeof line)) std::snprintf(line + n, sizeof line - size_t(n), " est. %.2f ms/f\n", totalMs);
    mkw_log("%s", line);
    std::vector<std::pair<uint32_t, uint32_t>> pages;
    uint32_t covered = 0;
    for (uint32_t page = 0; page < 4096; ++page) {
        if (MemoryInline::g_deferredReadCoveredPages[page]) ++covered;
        if (const uint32_t count = now.pages[page] - then.pages[page]) pages.emplace_back(count, page);
    }
    std::sort(pages.begin(), pages.end(), std::greater<>());
    n = std::snprintf(line, sizeof line, "[mkw-fprof] deferred-covered 1 MiB pages now %u; cold accesses by page:", covered);
    for (size_t i = 0; i < pages.size() && i < 16; ++i)
        if (n > 0 && n < int(sizeof line))
            n += std::snprintf(line + n, sizeof line - size_t(n), " %08x%s %.0f/f;", pages[i].second << 20,
                               MemoryInline::g_deferredReadCoveredPages[pages[i].second] ? "*" : "", double(pages[i].first) / frames);
    mkw_log("%s\n", line);
}

void report(ThreadState* s) {
    const uint64_t now = tsc();
    const double freq = double(sceKernelGetTscFrequency());
    const double frames = double(g_window.frames);
    const auto msPerFrame = [&](double ticks) { return ticks * 1e3 / freq / frames; };
    // Charge the current top so its self time is complete at the cut.
    charge(s, now);
    s->last = now;
    std::vector<Stat> rows;
    rows.reserve(8192);
    double translatedSelf = 0, nativeSelf = 0;
    uint64_t translatedCalls = 0, nativeCalls = 0;
    for (size_t i = 0; i < kTableSize; ++i) {
        const Stat& e = s->table[i];
        if (!e.key || (!e.calls && !e.self)) continue;
        rows.push_back(e);
        if (e.key & kNativeBit) { nativeSelf += double(e.self); nativeCalls += e.calls; }
        else { translatedSelf += double(e.self); translatedCalls += e.calls; }
    }
    const double wall = double(now - g_window.startTsc);
    const double hook = double(s->hook - g_window.startHook);
    const double bias = g_biasTicksPerCall;
    mkw_log("[mkw-fprof] window %u: %u frames, process-time %.1f s, wall %.2f ms/f; translated self %.2f ms/f in %.0f calls/f; "
            "native (abi_bridge) self %.2f ms/f in %.0f calls/f; outside guest calls %.2f ms/f; hooks %.2f ms/f excluded "
            "(%.0f events/f), residual estimate %.2f ms/f; unmatched %llu unwound %llu overflow %llu"
            " fiber-switches %.1f/f; %zu functions\n",
            g_window.index, g_window.frames, double(sceKernelGetProcessTime()) * 1e-6, msPerFrame(wall),
            msPerFrame(translatedSelf), double(translatedCalls) / frames, msPerFrame(nativeSelf), double(nativeCalls) / frames,
            msPerFrame(double(s->root - g_window.startRoot)), msPerFrame(hook), double(s->events - g_window.startEvents) / frames,
            msPerFrame(bias * 2.0 * double(translatedCalls + nativeCalls)),
            (unsigned long long)(s->unmatched - g_window.startUnmatched), (unsigned long long)(s->unwound - g_window.startUnwound),
            (unsigned long long)(s->overflows - g_window.startOverflows),
            double(s->switches - g_window.startSwitches) / frames, rows.size());
    report_slow_accesses(frames, freq);
    std::sort(rows.begin(), rows.end(), [](const Stat& a, const Stat& b) { return a.self > b.self; });
    mkw_log("[mkw-fprof] top self: rank self-ms/f self%% adj-ms/f incl-ms/f calls/f ns/call name (guest)\n");
    for (size_t i = 0; i < rows.size() && i < kTopSelf; ++i) {
        const Stat& e = rows[i];
        const Named n = describe(e.key);
        const double adjusted = std::max(0.0, double(e.self) - bias * double(e.calls + e.childCalls));
        mkw_log("[mkw-fprof] %2zu %7.3f %5.2f%% %7.3f %7.3f %9.1f %8.1f %s (0x%08x)\n", i + 1, msPerFrame(double(e.self)),
                100.0 * double(e.self) / wall, msPerFrame(adjusted), msPerFrame(double(e.inclusive)),
                double(e.calls) / frames, e.calls ? double(e.self) * 1e9 / freq / double(e.calls) : 0.0,
                n.name.c_str(), n.guest);
    }
    std::vector<Stat> inclusive(rows);
    std::sort(inclusive.begin(), inclusive.end(), [](const Stat& a, const Stat& b) { return a.inclusive > b.inclusive; });
    mkw_log("[mkw-fprof] top inclusive: rank incl-ms/f self-ms/f calls/f name (guest)\n");
    for (size_t i = 0; i < inclusive.size() && i < kTopInclusive; ++i) {
        const Stat& e = inclusive[i];
        const Named n = describe(e.key);
        mkw_log("[mkw-fprof] i%-2zu %7.3f %7.3f %9.1f %s (0x%08x)\n", i + 1, msPerFrame(double(e.inclusive)),
                msPerFrame(double(e.self)), double(e.calls) / frames, n.name.c_str(), n.guest);
    }
    // Full table for offline analysis, written off the guest thread.
    auto csv = std::make_shared<std::string>();
    csv->reserve(rows.size() * 96 + 256);
    char line[400];
    std::snprintf(line, sizeof line, "# window %u frames %u wall_ns %.0f tsc_hz %.0f bias_ns_per_call %.3f anchor %p\n"
                  "key,guest,calls,child_calls,self_ns,inclusive_ns,name\n",
                  g_window.index, g_window.frames, wall * 1e9 / freq, freq, bias * 1e9 / freq,
                  reinterpret_cast<void*>(&mkw_fprof_frame_end));
    csv->append(line);
    for (const Stat& e : rows) {
        const Named n = describe(e.key);
        std::snprintf(line, sizeof line, "0x%llx,0x%08x,%llu,%llu,%.0f,%.0f,\"%s\"\n", (unsigned long long)e.key, n.guest,
                      (unsigned long long)e.calls, (unsigned long long)e.childCalls, double(e.self) * 1e9 / freq, double(e.inclusive) * 1e9 / freq, n.name.c_str());
        csv->append(line);
    }
    const unsigned index = g_window.index;
    std::thread([csv, index] {
        char path[96];
        std::snprintf(path, sizeof path, "/app0/UserData/Logs/fprof-%03u.csv", index);
        if (std::FILE* file = std::fopen(path, "wb")) { std::fwrite(csv->data(), 1, csv->size(), file); std::fclose(file); }
    }).detach();
    ++g_window.index;
    begin_window(s);
    // The report itself is not charged to the function on top.
    const uint64_t after = tsc();
    s->stack->excluded += after - now;
    s->last = after;
}
}  // namespace

extern "C" {
__attribute__((no_instrument_function)) void __cyg_profile_func_enter(void* function, void*) {
    if (!g_enabled) return;
    const uint64_t t0 = tsc();
    ThreadState* s = state();
    push(s, reinterpret_cast<uintptr_t>(function), t0);
    finish_hook(s, t0);
}

__attribute__((no_instrument_function)) void __cyg_profile_func_exit(void* function, void*) {
    if (!g_enabled) return;
    const uint64_t t0 = tsc();
    ThreadState* s = state();
    pop(s, reinterpret_cast<uintptr_t>(function), t0);
    finish_hook(s, t0);
}

void mkw_fprof_native_enter(uint32_t site) {
    if (!g_enabled) return;
    const uint64_t t0 = tsc();
    ThreadState* s = state();
    push(s, kNativeBit | site, t0);
    finish_hook(s, t0);
}

void mkw_fprof_native_exit(uint32_t site) {
    if (!g_enabled) return;
    const uint64_t t0 = tsc();
    ThreadState* s = state();
    pop(s, kNativeBit | site, t0);
    finish_hook(s, t0);
}

// Called by HostContext::Switch before the host stack changes; the returned
// shadow stack is kept by the outgoing fiber and handed back on resumption.
void* mkw_fprof_fiber_leave() {
    if (!g_enabled || !t_state) return nullptr;
    const uint64_t now = tsc();
    ThreadState* s = t_state;
    charge(s, now);
    s->last = now;
    s->stack->leftAt = now;
    ++s->switches;
    return s->stack;
}

// Called on the incoming fiber: with its own stack when it resumes inside
// Switch, with nullptr when it starts (a fresh stack) or was suspended before
// profiling began. A finished fiber's stack is simply not resumed.
void mkw_fprof_fiber_enter(void* stack) {
    if (!g_enabled) return;
    const uint64_t now = tsc();
    ThreadState* s = state();
    auto* k = static_cast<Stack*>(stack);
    if (k) k->excluded += now - k->leftAt; else k = new_stack();
    s->stack = k;
    s->last = now;
}

void mkw_fprof_frame_end() {
    ++g_framesSeen;
    if (!g_enabled) {
        if (g_framesSeen < kWarmupFrames) return;
        calibrate();
        ThreadState* s = state();
        g_owner = s;
        g_enabled = true;
        s->last = tsc();
        begin_window(s);
        mkw_log("[mkw-fprof] DIAGNOSTIC function profiler enabled at frame %u (window %u frames, anchor %p)\n",
                g_framesSeen, kWindow, reinterpret_cast<void*>(&mkw_fprof_frame_end));
        return;
    }
    ThreadState* s = t_state;
    if (!s || s != g_owner) return;
    if (++g_window.frames >= kWindow) report(s);
}
}
