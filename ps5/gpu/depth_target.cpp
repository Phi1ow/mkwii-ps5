// SPDX-License-Identifier: GPL-3.0-only
// Field setters from requested SharpProspero CxDepthRenderTarget.cs.
#include "depth_target.h"
#include <algorithm>
#include <bit>
#include <stdexcept>
namespace mkw::agc {
std::array<uint32_t,16> encode_depth_target(std::span<const uint32_t,16> defaults,uint32_t w,uint32_t h,uint64_t address){
    if(!w||!h||w>16384||h>16384||(address&65535)||address>=(uint64_t{1}<<48))
        throw std::invalid_argument("Invalid AGC depth extent/address");
    std::array<uint32_t,16> r;std::copy(defaults.begin(),defaults.end(),r.begin());
    auto set=[&](unsigned i,uint32_t mask,uint32_t value){r[i]=(r[i]&~mask)|value;};
    set(0,0x000f0000,0);set(0,3,3);set(0,0xc,0);
    set(0,0x20000000,0);set(0,0x08000000,0);set(0,0x80000000,0);
    set(0,0x00100800,0);set(0,0x1000,0);set(0,0x03800000,0);
    set(1,1,0);set(1,0x00100800,0);set(1,0x20000000,0x20000000);
    set(1,0x1000,0);set(1,0x08000000,0);
    set(11,0x1fff,0);set(11,0xc0ffe000,0);set(11,0x3c000000,0);
    set(11,0x01000000,0);set(11,0x02000000,0x02000000);
    set(13,0x3fff,w-1);set(13,0x3fff0000,(h-1)<<16);
    r[14]=std::bit_cast<uint32_t>(1.f);set(15,0xff,0);
    r[2]=r[4]=uint32_t(address>>8);set(6,0xff,uint32_t(address>>40));set(8,0xff,uint32_t(address>>40));
    r[3]=r[5]=r[12]=0;set(7,0xff,0);set(9,0xff,0);set(10,0xff,0);
    return r;
}
}
