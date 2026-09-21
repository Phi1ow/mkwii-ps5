// SPDX-License-Identifier: GPL-3.0-only
#include "async_log.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdarg>
#include <cstdio>
#include <mutex>
#include <string>
#include <thread>
#include <unistd.h>

namespace {
constexpr size_t kQueueLimit = 1u << 20;  // bytes waiting for the writer
constexpr size_t kLineLimit = 2048;
constexpr auto kSlowWrite = std::chrono::milliseconds(100);
// Lines arriving within this delay share one write call: each system call is
// expensive on this console (~22 us measured for the cheapest ones).
constexpr auto kBatchDelay = std::chrono::milliseconds(500);

struct Queue {
    std::mutex mutex;
    std::condition_variable wake;
    std::string pending;
    std::atomic<uint64_t> dropped{0};
    std::atomic<uint64_t> maxWriteNanos{0};

    Queue() {
        pending.reserve(kQueueLimit);
        std::thread([this] { run(); }).detach();
    }

    void push(const char* text, size_t size) {
        {
            std::lock_guard lock(mutex);
            if (pending.size() + size > kQueueLimit) {
                dropped.fetch_add(1, std::memory_order_relaxed);
                return;
            }
            pending.append(text, size);
        }
        wake.notify_one();
    }

    void run() {
        std::string writing;
        writing.reserve(kQueueLimit);
        for (;;) {
            {
                std::unique_lock lock(mutex);
                wake.wait(lock, [this] { return !pending.empty(); });
                lock.unlock();
                std::this_thread::sleep_for(kBatchDelay);
                lock.lock();
                writing.swap(pending);  // both keep their reserved capacity
            }
            const auto start = std::chrono::steady_clock::now();
            // stderr is unbuffered: writing its descriptor directly keeps the
            // file offset shared with other writers and makes one call per batch.
            const int fd = fileno(stderr);
            for (size_t done = 0; done < writing.size();) {
                const ssize_t n = ::write(fd, writing.data() + done, writing.size() - done);
                if (n <= 0) break;
                done += size_t(n);
            }
            const auto elapsed = std::chrono::steady_clock::now() - start;
            const uint64_t nanos = uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count());
            uint64_t seen = maxWriteNanos.load(std::memory_order_relaxed);
            while (nanos > seen && !maxWriteNanos.compare_exchange_weak(seen, nanos, std::memory_order_relaxed)) {}
            if (elapsed >= kSlowWrite) {
                char line[160];
                const int n = std::snprintf(line, sizeof line, "[mkw-log] writing %zu bytes to stderr took %.0f ms\n",
                                            writing.size(), double(nanos) * 1e-6);
                if (n > 0) push(line, size_t(n) < sizeof line ? size_t(n) : sizeof line - 1);
            }
            writing.clear();
        }
    }
};

Queue& queue() {
    static Queue* instance = new Queue();  // never destroyed: the writer thread outlives static teardown
    return *instance;
}
}  // namespace

extern "C" void mkw_log(const char* format, ...) {
    char line[kLineLimit];
    va_list args;
    va_start(args, format);
    const int n = std::vsnprintf(line, sizeof line, format, args);
    va_end(args);
    if (n <= 0) return;
    queue().push(line, size_t(n) < sizeof line ? size_t(n) : sizeof line - 1);
}

extern "C" uint64_t mkw_log_dropped(void) { return queue().dropped.load(std::memory_order_relaxed); }
extern "C" uint64_t mkw_log_max_write_nanos(void) { return queue().maxWriteNanos.load(std::memory_order_relaxed); }
