// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "gx_texture_cache.h"
#include "gx_copy_texture_cache.h"
#include "gx_tev_program.h"
#include "gx_fog.h"

namespace mkw::agc {
struct alignas(16) GxSampleBinding {
    std::array<uint32_t,8> texture{};
    std::array<uint32_t,4> sampler{};
    // Inverse texture dimensions in GX's 1/128 texel domain, shader LOD bias.
    std::array<float,4> parameters{};
};
struct alignas(16) GxDirectUniforms {
    MkwTevProgram program{};
    std::array<GxSampleBinding,8> textures{};
    std::array<std::array<float,4>,8> coordinateScales{};
    // Indirect stages: {texture map, texture coordinate, S scale shift, T scale shift}.
    std::array<std::array<uint32_t,4>,4> indirectStages{};
    // Indirect matrices in the reference layout: [2m]={m0.x,m0.y,m1.x,m1.y} S10 mantissas,
    // [2m+1]={m2.x,m2.y,-scaleExp,0}.
    std::array<std::array<int32_t,4>,6> indirectMatrices{};
    MkwFog fog{};
};
struct GxSourceBytes {
    std::span<const uint8_t> texture;
    std::span<const uint8_t> palette;
};
// The memory owner must validate guest/host ranges before supplying these
// spans. This API never dereferences GX object data pointers. Bytes must remain
// stable during construction; afterward the material owns its GPU copies.
using GxSourceResolver=GxSourceBytes (*)(void*,const GXTexObj_&,const GXTlutObj_*);
class GxDirectMaterial {
public:
    const GxDirectUniforms& uniforms() const noexcept { return uniforms_; }
    uint32_t texture_mask() const noexcept { return mask_; }
    const std::array<TextureHandle,8>& textures() const noexcept { return textures_; }
    const std::array<ColorCopyHandle,8>& color_copies() const noexcept { return colorCopies_; }
    // Keep this object, AND the uploaded uniform buffer, alive until all draws
    // using them have retired. Cache eviction does not release these textures.
    static GxDirectMaterial build(const aurora::gx::GXRegisterState&,GxTextureCache&,
        GxSourceResolver,void* context,uint16_t configuredAnisotropy,bool viewportLodBias=true,
        GxCopyTextureCache* copies=nullptr);
    // Same, reusing a program already built from this state by the caller.
    static GxDirectMaterial build(const MkwTevProgram& program,const aurora::gx::GXRegisterState&,GxTextureCache&,
        GxSourceResolver,void* context,uint16_t configuredAnisotropy,bool viewportLodBias=true,
        GxCopyTextureCache* copies=nullptr);
private:
    GxDirectUniforms uniforms_{};
    std::array<TextureHandle,8> textures_{};
    std::array<ColorCopyHandle,8> colorCopies_{};
    uint32_t mask_=0;
};
}
