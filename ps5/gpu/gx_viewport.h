// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "gx/register_state.hpp"

namespace mkw::agc {
struct GxRenderExtent {
    uint32_t logicalWidth=640,logicalHeight=480,targetWidth=640,targetHeight=480;
};
struct GxMappedViewport {
    aurora::gfx::Viewport viewport;
    aurora::gfx::ClipRect scissor;
};
// Mapping follows WiiCompiled gx.cpp: wrapped BP scissor offsets are selected
// by viewport overlap, then logical pixels scale to the current render target.
// FIT/STRETCH presentation policy is applied later, outside this EFB mapping.
GxMappedViewport map_gx_viewport(const aurora::gx::GXRegisterState&,GxRenderExtent) noexcept;
// Call at render-target transitions, after retiring commands using the old
// extent. An offscreen target uses its own size for both logical and target.
void configure_gx_render_extent(GxRenderExtent);
GxRenderExtent current_gx_render_extent();
struct GxContextRegister {uint16_t offset,pad;uint32_t value;};
static_assert(sizeof(GxContextRegister)==8);
// Pure owned snapshot; invalid/nonfinite viewports are rejected before AGC.
std::array<GxContextRegister,14> snapshot_gx_viewport(const GxMappedViewport&);
}
