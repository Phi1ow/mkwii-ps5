// SPDX-License-Identifier: GPL-3.0-only
#include "gx_viewport.h"
#include "gx/register_backend.hpp"
#include <mutex>
#include <stdexcept>
namespace {
mkw::agc::GxRenderExtent extent;
void apply() noexcept {
    auto& s=aurora::gx::register_state();
    auto mapped=mkw::agc::map_gx_viewport(s,extent);
    aurora::gx::set_render_viewport(mapped.viewport);
    aurora::gx::set_render_scissor(mapped.scissor);
}
}
namespace mkw::agc {
GxRenderExtent current_gx_render_extent(){std::lock_guard lock(aurora::renderer_gpu_mutex());return extent;}
void configure_gx_render_extent(GxRenderExtent value) {
    if(!value.logicalWidth||!value.logicalHeight||!value.targetWidth||!value.targetHeight||
       value.logicalWidth>32767||value.logicalHeight>32767||value.targetWidth>32767||value.targetHeight>32767)
        throw std::invalid_argument("Invalid GX render-target extent");
    std::lock_guard lock(aurora::renderer_gpu_mutex());extent=value;apply();
}
}
namespace aurora::gx {
// FIFO dispatch already holds renderer_gpu_mutex; values are copied into each
// draw's register snapshot before subsequent GX commands can change them.
void set_render_viewport(const gfx::Viewport& value) noexcept {
    auto& s=register_state();if(s.renderViewport!=value){s.renderViewport=value;s.stateDirty=true;}
}
void set_render_scissor(const gfx::ClipRect& value) noexcept {
    auto& s=register_state();if(s.renderScissor!=value){s.renderScissor=value;s.stateDirty=true;}
}
void set_logical_viewport(const gfx::Viewport& value) noexcept {
    auto& s=register_state();if(s.logicalViewport!=value){s.logicalViewport=value;s.stateDirty=true;}apply();
}
void set_logical_scissor(const gfx::ClipRect& value) noexcept {
    register_state().logicalScissor=value;apply();
}
}
