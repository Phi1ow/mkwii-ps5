// SPDX-License-Identifier: GPL-3.0-only
// Diagnostic lines written to stderr without blocking the calling thread.
//
// On the console stderr is an unbuffered file under /data. Measured in race
// (artifacts/game-native/99611-watchdog): the process keeps being scheduled
// (the sampler's 1 ms sleeps never wake late) while the frame loop sits in the
// frame report's fprintf for 15-19 s, once per report window. mkw_log formats
// the line on the caller's stack, appends it to a bounded in-memory queue and
// returns; one writer thread does the file writes and reports its own slow
// writes. Lines beyond the queue bound are dropped and counted, never waited for.
#pragma once
#include <cstdint>

#ifdef __cplusplus
extern "C" {
#endif
void mkw_log(const char* format, ...) __attribute__((format(printf, 1, 2)));
// Lines dropped because the queue was full, and the slowest write so far.
uint64_t mkw_log_dropped(void);
uint64_t mkw_log_max_write_nanos(void);
#ifdef __cplusplus
}
#endif
