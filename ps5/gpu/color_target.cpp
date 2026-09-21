// SPDX-License-Identifier: GPL-3.0-only
// Register fields: requested SharpProspero AgcRenderTargetSetup/CxRenderTarget.
#include "color_target.h"
#include <algorithm>
#include <stdexcept>
namespace mkw::agc {
std::array<uint32_t,16> encode_color_target(std::span<const uint32_t,16> defaults,
    uint32_t width,uint32_t height,uint64_t address) {
    if(!width||!height||width>16384||height>16384||(address&65535)||address>=(uint64_t{1}<<48))
        throw std::invalid_argument("Invalid AGC color target extent/address");
    std::array<uint32_t,16> r;std::copy(defaults.begin(),defaults.end(),r.begin());
    auto set=[&](unsigned i,uint32_t mask,uint32_t value){r[i]=(r[i]&~mask)|value;};
    set(1,0x03ffe000,0);
    set(2,0x0000007c,0x28);set(2,0x700,0);set(2,0x1800,0x800);
    set(2,0x10000,0);set(2,0x8000,0x8000);set(2,0x40000,0);
    set(2,0x10000000,0);set(2,0x4000,0);set(2,0x04000000,0);
    set(3,0x7000,0);set(3,0x18000,0);
    set(4,0xc,8);set(4,0x60,0x40);set(4,0x00100200,0);set(4,0x80000,0);
    set(14,0x3fff,height-1);set(14,0x0fffc000,(width-1)<<14);set(14,0xf0000000,0);
    set(15,0x1fff,0);set(15,0x7c000,0x6c000);set(15,0x03000000,0x01000000);set(15,0x44000000,0x44000000);
    r[0]=uint32_t(address>>8);set(10,0xff,uint32_t(address>>40));
    r[5]=r[6]=r[9]=0;set(11,0xff,0);set(12,0xff,0);set(13,0xff,0);
    return r;
}
}
