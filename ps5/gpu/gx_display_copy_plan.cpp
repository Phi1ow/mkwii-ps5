// SPDX-License-Identifier: GPL-3.0-only
#include "gx_display_copy.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace mkw::agc {
GxTextureCopyPlan plan_gx_display_copy(const aurora::gx::GXRegisterState& s,GxRenderExtent e,bool clear,bool disableFilter){
    if(!e.logicalWidth||!e.logicalHeight||!e.targetWidth||!e.targetHeight||
       e.logicalWidth>16384||e.logicalHeight>16384||e.targetWidth>16384||e.targetHeight>16384)
        throw std::invalid_argument("Invalid display-copy extent");
    const auto& r=s.dispCopySrc;
    if(r.x<0||r.y<0||r.width<=0||r.height<=0)
        throw std::invalid_argument("Invalid display-copy source");
    // Aurora map_logical_scissor uses floor for lower edges, ceil for upper
    // edges and clips to the target. Display copies
    // do not use the fractional texture-copy source rectangle or BP offsets.
    const bool native=s.viewportPolicy==AURORA_VIEWPORT_NATIVE;
    auto edge=[&](int64_t v,uint32_t logical,uint32_t target,bool upper){
        if(native)return v;
        const float scaled=float(v)*float(target)/float(logical);
        return int64_t(std::clamp(upper?std::ceil(scaled):std::floor(scaled),0.f,float(target)));
    };
    const auto left=edge(r.x,e.logicalWidth,e.targetWidth,false),top=edge(r.y,e.logicalHeight,e.targetHeight,false);
    const auto right=edge(int64_t(r.x)+r.width,e.logicalWidth,e.targetWidth,true);
    const auto bottom=edge(int64_t(r.y)+r.height,e.logicalHeight,e.targetHeight,true);
    if(left>=right||top>=bottom||right>e.targetWidth||bottom>e.targetHeight)
        throw std::invalid_argument("Display-copy source exceeds EFB");
    auto copy=s;
    copy.texCopySrc={int32_t(left),int32_t(top),int32_t(std::max<int64_t>(right-left,1)),int32_t(std::max<int64_t>(bottom-top,1))};
    copy.texCopySrcRenderSpace=true;
    copy.texCopyDstWidth=s.dispCopyDstWidth?s.dispCopyDstWidth:r.width;
    copy.texCopyDstHeight=s.dispCopyDstHeight?s.dispCopyDstHeight:r.height;
    copy.texCopyFmt=GX_TF_RGBA8;copy.texCopyHalfScale=false;copy.dstAlpha=UINT32_MAX;
    auto p=plan_gx_texture_copy(copy,e,clear);
    // Unlike GXCopyTex, upstream GXCopyDisp passes forceOpaqueAlpha=false and
    // does not overwrite EFB alpha before copying. Preserve that distinction.
    p.options.forceOpaqueAlpha=false;
    // resolve_pass in the supplied Aurora clamps this to at least one target
    // texel, including when the internal render size is below Wii resolution.
    p.options.rowStride=std::max(float(copy.texCopySrc.height)/r.height,1.f);
    if(disableFilter){const auto& f=p.options.filter;p.options.filter={0,f[0]+f[1]+f[2],0};}
    const bool filtered=p.options.filter!=std::array<uint32_t,3>{0,64,0};
    p.options.linear=!filtered&&(p.source.width!=p.scaledWidth||p.source.height!=p.scaledHeight);
    return p;
}
}
