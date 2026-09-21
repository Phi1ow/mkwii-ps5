// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <array>
#include <cstdint>
namespace mkw::agc {
// A graphics end-of-pipe completion write, derived from the native AGC
// SetFlip packet captured on this PS5 Pro 9.40 (PPSA99548). No display event
// or interrupt is requested. Caller owns the aligned GPU-visible label,
// command storage and every resource until the requested serial is observed.
std::array<uint32_t,8> encode_gpu_completion(uint64_t labelAddress,uint64_t serial);
}
