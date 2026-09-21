// SPDX-License-Identifier: GPL-3.0-only
// Equation 27 from the requested SharpProspero AgcTilingTables.cs,
// Copyright (C) 2026 SvenGDK. D32Float, mode 24, 2D, one sample/slice/mip.
#include "depth_layout.h"
#include "gx_depth_offset.h"
#include <stdexcept>
namespace mkw::agc {
DepthLayout::DepthLayout(uint32_t w,uint32_t h):width_(w),height_(h){
    if(!w||!h||w>16384||h>16384)throw std::invalid_argument("Invalid AGC depth dimensions");
    paddedWidth_=(w+127)&~127u;
    bytes_=size_t(paddedWidth_)*((h+127)&~127u)*4;
}
size_t DepthLayout::pixel_offset(uint32_t x,uint32_t y) const {
    if(x>=width_||y>=height_)throw std::out_of_range("AGC depth texel coordinate");
    return mkw_depth_pixel_offset(x,y,paddedWidth_>>7);
}
}
