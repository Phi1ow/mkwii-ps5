// SPDX-License-Identifier: GPL-3.0-only
// Guest (main) thread placement on the console's processors.
//
// Every translated game step runs on the single host thread that owns the
// guest fibers. The kernel places it like any other thread; this file reports
// that placement and, only when /app0/UserData/placement.txt exists, confines
// the calling thread to one processor and/or sets its priority, so the effect
// can be measured run by run. File contents: "core <n>" and/or
// "priority <p>" (256 most urgent .. 767 least, 700 default), whitespace separated.
// APIs as bound by SharpProspero Interop/Kernel/KernelThread.cs and KernelClock.cs.
#include "async_log.h"
#include <cstdint>
#include <cstdio>
#include <cstring>

extern "C" {
void* scePthreadSelf();
int scePthreadGetaffinity(void* thread, uint64_t* mask);
int scePthreadSetaffinity(void* thread, uint64_t mask);
int scePthreadGetprio(void* thread, int* priority);
int scePthreadSetprio(void* thread, int priority);
int sceKernelGetCurrentCpu();
}

extern "C" void mkw_report_thread_placement(const char* who) {
    uint64_t mask = 0;
    int priority = 0;
    const int affinityRc = scePthreadGetaffinity(scePthreadSelf(), &mask);
    const int priorityRc = scePthreadGetprio(scePthreadSelf(), &priority);
    mkw_log("[mkw-thread] %s: affinity 0x%llx (rc %08x), priority %d (rc %08x), on cpu %d\n", who,
            (unsigned long long)mask, unsigned(affinityRc), priority, unsigned(priorityRc), sceKernelGetCurrentCpu());
}

extern "C" void mkw_apply_thread_placement_if_requested() {
    std::FILE* file = std::fopen("/app0/UserData/placement.txt", "rb");
    if (!file) return;
    char text[128] = {};
    const size_t read = std::fread(text, 1, sizeof text - 1, file);
    std::fclose(file);
    text[read] = 0;
    int core = -1, priority = -1;
    for (char* token = std::strtok(text, " \t\r\n"); token; token = std::strtok(nullptr, " \t\r\n")) {
        char* value = std::strtok(nullptr, " \t\r\n");
        if (!value) break;
        if (!std::strcmp(token, "core")) core = std::atoi(value);
        else if (!std::strcmp(token, "priority")) priority = std::atoi(value);
    }
    mkw_report_thread_placement("guest thread before placement");
    if (core >= 0 && core < 64) {
        const int rc = scePthreadSetaffinity(scePthreadSelf(), uint64_t(1) << core);
        mkw_log("[mkw-thread] DIAGNOSTIC confine guest thread to cpu %d: rc %08x\n", core, unsigned(rc));
    }
    if (priority >= 256 && priority <= 767) {
        const int rc = scePthreadSetprio(scePthreadSelf(), priority);
        mkw_log("[mkw-thread] DIAGNOSTIC guest thread priority %d: rc %08x\n", priority, unsigned(rc));
    }
    mkw_report_thread_placement("guest thread after placement");
}
