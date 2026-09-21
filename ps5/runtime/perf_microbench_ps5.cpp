// SPDX-License-Identifier: GPL-3.0-only
// DIAGNOSTIC: one-shot micro-benchmark on the console, run at Aurora start-up
// only when /app0/UserData/microbench.txt exists. It adds the guest-memory
// services used by every GX source read to the platform-neutral primitives.
#include "perf_microbench.h"
#include "gx_memory_sources.h"
#include "memory.h"
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <ctime>
#include <exception>
#include <thread>
#include <vector>

// Timing and sleep candidates from the PS5 libkernel catalog (ps5link-sdk
// linker/catalog.c, SharpProspero Interop). Measured, never assumed fast.
extern "C" {
uint64_t sceKernelReadTsc();
uint64_t sceKernelGetTscFrequency();
uint64_t sceKernelGetProcessTimeCounter();
uint64_t sceKernelGetProcessTimeCounterFrequency();
uint64_t sceKernelGetProcessTime();
int sceKernelClockGettime(int, struct timespec*);
int sceKernelUsleep(unsigned);
int sched_yield();
}

extern "C" void mkw_run_perf_microbench_if_requested() {
    if (std::FILE* flag = std::fopen("/app0/UserData/microbench.txt", "rb")) {
        std::fclose(flag);
    } else {
        return;
    }
    using namespace mkw::agc::microbench;
    std::fprintf(stderr, "[mkw-microbench] DIAGNOSTIC start\n");
    try {
        run_core(stderr);
        // Clock candidates. CLOCK_MONOTONIC_FAST is FreeBSD id 12.
        measure(stderr, "clock_gettime(MONOTONIC)", 20000, [](unsigned) { timespec t{}; clock_gettime(CLOCK_MONOTONIC, &t); g_sink += uint64_t(t.tv_nsec); });
        measure(stderr, "clock_gettime(MONOTONIC_FAST=12)", 20000, [](unsigned) { timespec t{}; clock_gettime(clockid_t(12), &t); g_sink += uint64_t(t.tv_nsec); });
        measure(stderr, "sceKernelClockGettime(MONOTONIC)", 20000, [](unsigned) { timespec t{}; sceKernelClockGettime(int(CLOCK_MONOTONIC), &t); g_sink += uint64_t(t.tv_nsec); });
        measure(stderr, "sceKernelGetProcessTime", 200000, [](unsigned) { g_sink += sceKernelGetProcessTime(); });
        measure(stderr, "sceKernelGetProcessTimeCounter", 200000, [](unsigned) { g_sink += sceKernelGetProcessTimeCounter(); });
        measure(stderr, "sceKernelReadTsc", 200000, [](unsigned) { g_sink += sceKernelReadTsc(); });
        const uint64_t counterHz = sceKernelGetProcessTimeCounterFrequency();
        const uint64_t tscHz = sceKernelGetTscFrequency();
        std::fprintf(stderr, "[mkw-microbench] process time counter frequency %llu Hz, TSC frequency %llu Hz\n",
                     (unsigned long long)counterHz, (unsigned long long)tscHz);
        // Sleep granularity, timed with the process time counter (not the slow clock).
        if (counterHz) {
            const auto actualMicros = [&](unsigned requested, unsigned count) {
                const uint64_t start = sceKernelGetProcessTimeCounter();
                for (unsigned i = 0; i < count; ++i) sceKernelUsleep(requested);
                return double(sceKernelGetProcessTimeCounter() - start) * 1e6 / double(counterHz) / count;
            };
            for (unsigned requested : {0u, 100u, 1000u, 4000u})
                std::fprintf(stderr, "[mkw-microbench] sceKernelUsleep(%u us) lasts %10.1f us on average\n", requested, actualMicros(requested, 100));
            const uint64_t start = sceKernelGetProcessTimeCounter();
            for (unsigned i = 0; i < 2000; ++i) sched_yield();
            std::fprintf(stderr, "[mkw-microbench] sched_yield lasts %10.1f us on average\n",
                         double(sceKernelGetProcessTimeCounter() - start) * 1e6 / double(counterHz) / 2000);
            const uint64_t sleepStart = sceKernelGetProcessTimeCounter();
            for (unsigned i = 0; i < 20; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(1));
            std::fprintf(stderr, "[mkw-microbench] this_thread::sleep_for(1 ms) lasts %10.1f us on average\n",
                         double(sceKernelGetProcessTimeCounter() - sleepStart) * 1e6 / double(counterHz) / 20);
        }
        // Guest memory services on a 64 KiB MEM1 range, like a texture source.
        const uint32_t guest = Memory::kMem1CachedBase + 0x00400000u;
        if (!Memory::Contains(guest, 65536)) {
            std::fprintf(stderr, "[mkw-microbench] MEM1 not mapped yet; guest memory probes skipped\n");
        } else {
            const auto* host = Memory::GetPointer(guest, 65536);
            measure(stderr, "Memory::Contains 64KiB", 20000, [&](unsigned) { g_sink += Memory::Contains(guest, 65536); });
            measure(stderr, "Memory::GetPointer 64KiB", 20000, [&](unsigned) { g_sink += uintptr_t(Memory::GetPointer(guest, 65536)); });
            measure(stderr, "ResolveDeferredReads 64KiB", 20000, [&](unsigned) { g_sink += MemoryInline::ResolveDeferredReads(guest, 65536); });
            measure(stderr, "guest_write_generation 64KiB", 20000, [&](unsigned) { g_sink += aurora::guest_write_generation(host, 65536); });
            measure(stderr, "GxMemorySources::read 64KiB", 20000, [&](unsigned) { g_sink += mkw::agc::GxMemorySources::read(host, 65536).size(); });
            measure(stderr, "GxMemorySources::read 64B", 20000, [&](unsigned) { g_sink += mkw::agc::GxMemorySources::read(host, 64).size(); });
            // Scalar guest accesses as translated code performs them on PS5
            // (checked tables) against a plain byte-swapped host load/store.
            // The store benchmarks write into live MEM1: keep and restore its bytes.
            std::vector<uint8_t> saved(host, host + 65536);
            measure(stderr, "Memory::Read32 (checked)", 2000000, [&](unsigned i) { g_sink += Memory::Read32(guest + ((i * 4u) & 0xfffcu)); });
            measure(stderr, "Memory::Write32 (checked)", 2000000, [&](unsigned i) { Memory::Write32(guest + ((i * 4u) & 0xfffcu), i); });
            measure(stderr, "host bswap load 32", 2000000, [&](unsigned i) { uint32_t v; std::memcpy(&v, host + ((i * 4u) & 0xfffcu), 4); g_sink += __builtin_bswap32(v); });
            measure(stderr, "host bswap store 32", 2000000, [&](unsigned i) { const uint32_t v = __builtin_bswap32(i); std::memcpy(const_cast<uint8_t*>(host) + ((i * 4u) & 0xfffcu), &v, 4); });
            std::memcpy(const_cast<uint8_t*>(host), saved.data(), saved.size());
        }
        // File writes. In race the unbuffered stderr log took 400 ms for 562 bytes,
        // later 54 s for 2.4 KiB (artifacts/game-native/99611-asynclog). Compare
        // write granularity, stdio buffering and the two paths of the same folder.
        if (counterHz) {
            const auto micros = [&](uint64_t start) { return double(sceKernelGetProcessTimeCounter() - start) * 1e6 / double(counterHz); };
            char payload[4096];
            std::memset(payload, 'x', sizeof payload);
            for (const char* path : {"/app0/UserData/Logs/microbench-write.bin", "/data/PPSA99611/UserData/Logs/microbench-write.bin"}) {
                const int fd = ::open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                if (fd < 0) {
                    std::fprintf(stderr, "[mkw-microbench] open %s failed errno=%d\n", path, errno);
                    continue;
                }
                uint64_t start = sceKernelGetProcessTimeCounter();
                for (unsigned i = 0; i < 64; ++i) (void)::write(fd, payload, 1);
                std::fprintf(stderr, "[mkw-microbench] %s write 1 B lasts %10.1f us\n", path, micros(start) / 64);
                start = sceKernelGetProcessTimeCounter();
                for (unsigned i = 0; i < 64; ++i) (void)::write(fd, payload, 562);
                std::fprintf(stderr, "[mkw-microbench] %s write 562 B lasts %10.1f us\n", path, micros(start) / 64);
                start = sceKernelGetProcessTimeCounter();
                for (unsigned i = 0; i < 16; ++i) (void)::write(fd, payload, sizeof payload);
                std::fprintf(stderr, "[mkw-microbench] %s write 4 KiB lasts %10.1f us (file now %u KiB)\n", path, micros(start) / 16, 16u * 4 + 37);
                ::close(fd);
            }
            for (const int mode : {_IONBF, _IOFBF}) {
                std::FILE* file = std::fopen("/app0/UserData/Logs/microbench-stdio.bin", "w");
                if (!file) break;
                std::setvbuf(file, nullptr, mode, 65536);
                const uint64_t start = sceKernelGetProcessTimeCounter();
                for (unsigned i = 0; i < 16; ++i) std::fprintf(file, "%.*s %u\n", 560, payload, i);
                std::fflush(file);
                std::fprintf(stderr, "[mkw-microbench] fprintf 562 B line (%s) lasts %10.1f us\n",
                             mode == _IONBF ? "unbuffered" : "64 KiB buffer, one flush", micros(start) / 16);
                std::fclose(file);
            }
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[mkw-microbench] aborted: %s\n", e.what());
    }
    std::fprintf(stderr, "[mkw-microbench] DIAGNOSTIC end\n");
}
