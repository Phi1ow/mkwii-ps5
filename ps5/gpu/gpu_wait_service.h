// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstdint>
namespace mkw::agc {
using GpuWaitCallback=void(*)();
void set_gpu_wait_callback(GpuWaitCallback);
void require_outside_gpu_wait_callback();
uint64_t gpu_clock_nanos();
void gpu_wait_slice(unsigned microseconds);
void gpu_wait_until(uint64_t deadlineNanos);
}
