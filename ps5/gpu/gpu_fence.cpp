// SPDX-License-Identifier: GPL-3.0-only
#include "gpu_fence.h"
#include <stdexcept>
namespace mkw::agc {
std::array<uint32_t,8> encode_gpu_completion(uint64_t address,uint64_t serial) {
    if(!address||(address&63)||address>=(uint64_t{1}<<48)||!serial)
        throw std::invalid_argument("GPU completion requires a dedicated aligned label and nonzero serial");
    // Preserve AGC's observed event/cache control. Change its interrupt-bearing
    // 0x42010000 to a plain 64-bit write and clear the display interrupt context.
    // Selector bit positions were cross-checked against AMD's PM4 reference;
    // the actual event/cache policy comes from this console's AGC recording.
    return {0xc0064900,0x06200504,0x40010000,uint32_t(address),uint32_t(address>>32),
            uint32_t(serial),uint32_t(serial>>32),0};
}
}
