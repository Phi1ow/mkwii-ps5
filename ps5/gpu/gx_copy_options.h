// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <array>
#include <cstdint>
namespace mkw::agc {
struct GxCopyOptions {
    uint32_t format=6; // GX_TF_RGBA8
    std::array<uint32_t,3> filter{0,64,0};
    float rowStride=1;
    bool linear=false,forceOpaqueAlpha=false,clampTop=false,clampBottom=false;
};
struct alignas(16) GxCopyUniforms {
    std::array<uint32_t,8> texture;
    std::array<uint32_t,4> sampler;
    std::array<float,4> filter; // three coefficients; normalized row step
    std::array<float,4> clamp; // top/bottom normalized coordinates
    std::array<uint32_t,4> flags; // filter active, opaque alpha, format, depth mode
    // Depth mode: raw V# of the D32 source and {width,height,blocksPerRow,0}.
    std::array<uint32_t,4> depth;
    std::array<uint32_t,4> dims;
};
static_assert(sizeof(GxCopyUniforms)==128);
}
