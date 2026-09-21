// SPDX-License-Identifier: GPL-3.0-only
#include "gx_video_state.h"
#include "gx/register_backend.hpp"
#include "gx/fifo.hpp"
#include <dolphin/vi.h>
#include <mutex>
#include <cmath>
#include <stdexcept>
namespace {
mkw::agc::GxVideoSettings settings;
std::optional<mkw::agc::GxRenderExtent> committed;
}
namespace mkw::agc {
GxVideoPlan snapshot_gx_video(){std::lock_guard lock(aurora::renderer_gpu_mutex());return plan_gx_video(settings);}
void commit_gx_video_size(std::optional<GxRenderExtent> value){std::lock_guard lock(aurora::renderer_gpu_mutex());committed=value;}
}
extern "C" {
void VIConfigure(const GXRenderModeObj* mode){
    std::lock_guard lock(aurora::renderer_gpu_mutex());auto next=settings;
    if(mode)next.mode=*mode;else next.mode.reset();
    (void)mkw::agc::plan_gx_video(next);settings=next;
}
void VISetFrameBufferScale(float scale){
    if(!std::isfinite(scale))throw std::invalid_argument("Nonfinite VI framebuffer scale");
    std::lock_guard lock(aurora::renderer_gpu_mutex());auto next=settings;next.scale=std::max(scale,0.f);
    (void)mkw::agc::plan_gx_video(next);settings=next;
}
void VILockAspectRatio(int width,int height){std::lock_guard lock(aurora::renderer_gpu_mutex());
    settings.aspectWidth=width>0&&height>0?width:0;settings.aspectHeight=width>0&&height>0?height:0;}
void VIUnlockAspectRatio(){VILockAspectRatio(0,0);}
void AuroraSetViewportPolicy(AuroraViewportPolicy policy){
    if(unsigned(policy)>AURORA_VIEWPORT_NATIVE)throw std::invalid_argument("Invalid GX viewport policy");
    if(aurora::gx::fifo::get_buffer_size())aurora::gx::fifo::drain();
    std::lock_guard lock(aurora::renderer_gpu_mutex());settings.policy=policy;
    auto& state=aurora::gx::register_state();state.viewportPolicy=policy;
    aurora::gx::set_logical_viewport(state.logicalViewport);aurora::gx::set_logical_scissor(state.logicalScissor);
}
void AuroraGetSurfaceSize(u32* width,u32* height){std::lock_guard lock(aurora::renderer_gpu_mutex());
    if(width)*width=settings.surfaceWidth;if(height)*height=settings.surfaceHeight;}
void AuroraGetRenderSize(u32* width,u32* height){std::lock_guard lock(aurora::renderer_gpu_mutex());
    const auto e=committed?*committed:mkw::agc::plan_gx_video(settings).extent;
    if(width)*width=e.targetWidth;if(height)*height=e.targetHeight;}
}
