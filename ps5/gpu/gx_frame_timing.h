// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstdint>
namespace mkw::agc {
void reset_gx_frame_timing();
uint64_t gx_present_deadline();
void record_gx_present();
}
