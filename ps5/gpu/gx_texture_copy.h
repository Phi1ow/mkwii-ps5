// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "agc_blit.h"
#include "gx_viewport.h"
#include "gx_copy_store.h"
#include <memory>
#include <vector>
namespace mkw::agc {
struct GxTextureCopyPlan {
    BlitSampleRect source;
    uint32_t logicalWidth,logicalHeight,scaledWidth,scaledHeight;
    GxCopyOptions options;
    BlitRect clearRect{};
    uint32_t clearMask=0,clearColor=0;
    uint32_t preCopyAlpha=UINT32_MAX;
    bool clearDepth=false;
    bool depth=false; // EFB depth source (Z16/Z24X8) rather than the color target
    float depthClearValue=0;
};
// Mapping and filter selection follow WiiCompiled GXFrameBuffer.cpp/common.cpp.
GxTextureCopyPlan plan_gx_texture_copy(const aurora::gx::GXRegisterState&,GxRenderExtent,bool clear);
// The renderer supplies its current EFB and a mandatory synchronizer that
// submits all previous draws ahead of the copy's blits on the GPU queue. Both
// the blitter and its prepared shader storage must outlive execution,
// including retained submissions after a GPU timeout.
class GxTextureCopyBackend {
public:
    using FinishDraws=void(*)(void*);
    GxTextureCopyBackend(AgcBlit&,std::shared_ptr<GpuColorTarget>,GxRenderExtent,FinishDraws,void*,
        std::shared_ptr<GpuDepthTarget> depth=nullptr);
    ColorCopyHandle execute(void* destination,const aurora::gx::GXRegisterState&,bool clear);
private:
    AgcBlit& blitter_;
    std::shared_ptr<GpuColorTarget> source_;
    std::shared_ptr<GpuDepthTarget> depth_;
    GxRenderExtent extent_;
    FinishDraws finish_;
    void* user_;
    // Copy destinations nothing else references any more (published copies,
    // materials and in-flight draw batches all hold owners) are reused: a new
    // target costs a 2 MiB direct-memory allocation and mapping.
    std::vector<std::shared_ptr<GpuColorTarget>> pool_;
    size_t poolBytes_=0;  // direct memory held by pool_
    std::shared_ptr<GpuColorTarget> pooled_target(uint32_t width,uint32_t height);
};
// Install while guest execution is excluded; no ownership transfer. Must be
// cleared before destroying the backend or changing the renderer's EFB.
void set_gx_texture_copy_backend(GxTextureCopyBackend*);
GxTextureCopyBackend* exchange_gx_texture_copy_backend(GxTextureCopyBackend*);
}
