// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "color_target.h"
#include "depth_target.h"
#include "gx_copy_options.h"
#include <memory>
namespace mkw::agc {
struct BlitRect { uint32_t x,y,width,height; };
struct BlitSampleRect { float x,y,width,height; };
enum class DepthCompare : uint32_t { Never,Less,Equal,LessEqual,Greater,NotEqual,GreaterEqual,Always };
struct DepthTest { float value=.5f; DepthCompare compare=DepthCompare::LessEqual; bool write=true; };
// Vertex ABI of the requested SDK's mesh vertex shader. NORMAL.xy carries UV.
struct BlitVertex { float position[3],normal[3],uv[2]; uint32_t color; };
static_assert(sizeof(BlitVertex)==36);
std::array<BlitVertex,4> blit_vertices(uint32_t sourceWidth,uint32_t sourceHeight,
    uint32_t targetWidth,uint32_t targetHeight,BlitRect source,BlitRect destination);
std::array<BlitVertex,4> sampled_blit_vertices(uint32_t sourceWidth,uint32_t sourceHeight,
    uint32_t targetWidth,uint32_t targetHeight,BlitSampleRect source,BlitRect destination);
// Asynchronous, single-threaded BGRA8 crop/scale using the SDK mesh VS and
// textured PS or the compiled GX copy PS. AGC must be initialized. Prepared shaders and their GPU storage
// remain caller-owned and must survive this object, including a timed-out draw.
// Source and destination must be distinct; neither may be displayed.
// Each call submits and returns; the GPU executes blits and draws in
// submission order. Owners stay alive until the blit completes. Call
// finish() before reading a destination on the CPU or scanning it out.
// Uses the AGC-initialized baseline for unbound raster state. Transitions from
// this port's GxRenderer and back were verified on PS5; arbitrary external AGC
// state is not supported. Subsequent draws must rebind state.
class AgcBlit {
public:
    enum class ShaderAbi { SdkTexture,GxCopy };
    AgcBlit(void* preparedVertexShader,void* preparedPixelShader,ShaderAbi=ShaderAbi::SdkTexture);
    ~AgcBlit();
    // True while a submitted blit has not completed, or after a failure.
    bool pending() const noexcept;
    // Waits (bounded) for every submitted blit; throws after a failure.
    void finish();
    AgcBlit(const AgcBlit&)=delete;
    AgcBlit& operator=(const AgcBlit&)=delete;
    uint64_t copy(std::shared_ptr<const GpuColorTarget> source,
        std::shared_ptr<GpuColorTarget> destination,BlitRect sourceRect,BlitRect destinationRect,
        const GxCopyOptions* options=nullptr);
    uint64_t copy_sampled(std::shared_ptr<const GpuColorTarget> source,
        std::shared_ptr<GpuColorTarget> destination,BlitSampleRect sourceRect,BlitRect destinationRect,
        const GxCopyOptions* options=nullptr);
    // GX depth copy: the D32 target is read through a raw buffer view (tiled
    // Tiled32_4) and converted to RGBA8 bytes by the GX copy shader
    // (options->format selects Z16/Z24X8). GxCopy ABI required.
    uint64_t copy_depth(std::shared_ptr<const GpuDepthTarget> source,
        std::shared_ptr<GpuColorTarget> destination,BlitSampleRect sourceRect,BlitRect destinationRect,
        const GxCopyOptions* options);
    // GPU rectangle fill using a one-texel constant source. Bits 0..3 of
    // writeMask select R/G/B/A; other channels and pixels are preserved.
    uint64_t clear_color(std::shared_ptr<GpuColorTarget> destination,BlitRect,
        uint32_t bgra,uint32_t writeMask=15);
    // Constant rectangle with D32 testing/writes. Color mask zero performs a
    // depth-only draw; Always + write implements a GPU depth rectangle clear.
    // The depth target must match the color extent and remain idle on entry.
    uint64_t fill_depth_tested(std::shared_ptr<GpuColorTarget> destination,
        std::shared_ptr<GpuDepthTarget> depth,BlitRect,uint32_t bgra,DepthTest,
        uint32_t colorWriteMask=15);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
