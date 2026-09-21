// SPDX-License-Identifier: GPL-3.0-only
#include "gx_texture_copy.h"
#include "gx_copy_color.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
namespace mkw::agc {
GxTextureCopyPlan plan_gx_texture_copy(const aurora::gx::GXRegisterState& s,GxRenderExtent e,bool clear){
    if(!e.logicalWidth||!e.logicalHeight||!e.targetWidth||!e.targetHeight||e.logicalWidth>16384||
       e.logicalHeight>16384||e.targetWidth>16384||e.targetHeight>16384)
        throw std::invalid_argument("Invalid EFB copy extent");
    const bool depthFormat=mkw_copy_depth_format(s.texCopyFmt);
    if(!mkw_copy_color_format(s.texCopyFmt)&&!depthFormat){
        if(aurora::gx::is_depth_format(s.texCopyFmt))
            throw std::invalid_argument("GX copy depth format requires unimplemented RAM encoding");
        throw std::invalid_argument("Invalid GX texture copy format");
    }
    const bool alpha=aurora::gx::render_target_has_alpha(s.pixelFmt);
    if(s.copyFilterAa)throw std::invalid_argument("GX multisample copy is not implemented");
    const auto& r=s.texCopySrc;
    if(r.x<0||r.y<0||r.width<=0||r.height<=0)
        throw std::invalid_argument("Invalid GX texture copy source");
    const bool native=s.viewportPolicy==AURORA_VIEWPORT_NATIVE;
    const float sx=native||s.texCopySrcRenderSpace?1.f:float(e.targetWidth)/e.logicalWidth;
    const float sy=native||s.texCopySrcRenderSpace?1.f:float(e.targetHeight)/e.logicalHeight;
    GxTextureCopyPlan p{};
    // Clip the sampled source to the EFB like Aurora's resolve_pass, keeping at
    // least one texel. A copy centred on a point near the screen edge (the race
    // lens flare, GameScreenEffectsMgr::CopyEFBToLensFlareTextures) legitimately
    // extends past it; the vertical filter row stride keeps the unclipped scale.
    const float unclippedHeight=r.height*sy;
    const auto clip=[](float start,float size,float limit){
        float low=std::clamp(start,0.f,limit),high=std::clamp(start+size,low,limit);
        if(high-low<1.f){high=std::min(low+1.f,limit);low=std::max(high-1.f,0.f);}
        return std::array<float,2>{low,high-low};
    };
    const auto [sourceX,sourceWidth]=clip(r.x*sx,r.width*sx,float(e.targetWidth));
    const auto [sourceY,sourceHeight]=clip(r.y*sy,r.height*sy,float(e.targetHeight));
    p.source={sourceX,sourceY,sourceWidth,sourceHeight};
    p.logicalWidth=std::max<uint32_t>(s.texCopyDstWidth,1);p.logicalHeight=std::max<uint32_t>(s.texCopyDstHeight,1);
    auto scale=[](uint32_t size,uint32_t logical,uint32_t target){
        const double value=std::round(double(size)*target/logical);
        if(value>16384)throw std::invalid_argument("Scaled GX copy exceeds AGC texture dimensions");
        return uint32_t(std::max(value,1.0));
    };
    p.scaledWidth=native?p.logicalWidth:scale(p.logicalWidth,e.logicalWidth,e.targetWidth);
    p.scaledHeight=native?p.logicalHeight:scale(p.logicalHeight,e.logicalHeight,e.targetHeight);
    // Validate bounds and fractional sample edges with the production mesh path.
    (void)sampled_blit_vertices(e.targetWidth,e.targetHeight,p.scaledWidth,p.scaledHeight,
        p.source,{0,0,p.scaledWidth,p.scaledHeight});
    if(alpha&&s.alphaUpdate&&s.dstAlpha!=UINT32_MAX){
        if(s.dstAlpha>255)throw std::invalid_argument("Invalid GX destination alpha");
        p.preCopyAlpha=s.dstAlpha;
    }
    if(clear){
        p.clearMask=(s.colorUpdate?7u:0u)|((s.alphaUpdate&&alpha)?8u:0u);
        // Match the source's independent nearest-edge rounding, then clip the
        // resulting clear rectangle to the EFB's raster bounds.
        auto edge=[&](int64_t v,uint32_t logical,uint32_t target){
            return native||s.texCopySrcRenderSpace?uint32_t(v):uint32_t(std::llround(double(v)*target/logical));
        };
        auto left=std::min(edge(r.x,e.logicalWidth,e.targetWidth),e.targetWidth-1);
        auto top=std::min(edge(r.y,e.logicalHeight,e.targetHeight),e.targetHeight-1);
        auto right=std::min(std::max(edge(int64_t(r.x)+r.width,e.logicalWidth,e.targetWidth),left+1),e.targetWidth);
        auto bottom=std::min(std::max(edge(int64_t(r.y)+r.height,e.logicalHeight,e.targetHeight),top+1),e.targetHeight);
        p.clearRect={left,top,right-left,bottom-top};
        if(!p.clearRect.width||!p.clearRect.height)p.clearMask=0;
        p.clearDepth=s.depthUpdate&&p.clearRect.width&&p.clearRect.height;
        if(p.clearDepth){
            // Same 24-bit normalization and reversed-Z convention as the
            // supplied WiiCompiled gx.hpp clear_depth_value()/projection.
            const float normalized=std::min(float(s.clearDepth)/16777216.f,16777215.f/16777216.f);
            p.depthClearValue=aurora::gx::UseReversedZ?1.f-normalized:normalized;
        }
        auto byte=[](float v){if(!std::isfinite(v))throw std::invalid_argument("Nonfinite GX clear color");
            return uint32_t(std::lround(std::clamp(v,0.f,1.f)*255.f));};
        if(p.clearMask&7)p.clearColor=(byte(s.clearColor.x())<<16)|(byte(s.clearColor.y())<<8)|byte(s.clearColor.z());
        if(p.clearMask&8)p.clearColor|=byte(s.clearColor.w())<<24;
    }
    p.options.format=s.texCopyFmt;
    if(s.copyFilterVf){const auto& f=s.copyFilterVFilter;
        for(auto value:f)if(value>63)throw std::invalid_argument("Invalid GX copy filter coefficient");
        p.options.filter={uint32_t(f[0])+f[1],uint32_t(f[2])+f[3]+f[4],uint32_t(f[5])+f[6]};}
    const bool filtered=p.options.filter!=std::array<uint32_t,3>{0,64,0};
    p.options.forceOpaqueAlpha=!alpha;
    const auto fractional=[](float value){return std::abs(value-std::round(value))>.01f;};
    const bool sampled=s.texCopyHalfScale||filtered||!alpha||fractional(p.source.x)||fractional(p.source.y)||
        fractional(p.source.width)||fractional(p.source.height)||
        std::abs(p.source.width-p.scaledWidth)>.01f||std::abs(p.source.height-p.scaledHeight)>.01f;
    p.options.linear=s.texCopyHalfScale||(sampled&&!filtered);
    p.options.rowStride=unclippedHeight/r.height;
    p.options.clampTop=(s.copyClamp&GX_CLAMP_TOP)!=0;p.options.clampBottom=(s.copyClamp&GX_CLAMP_BOTTOM)!=0;
    if(depthFormat){
        // Aurora resolves depth with texel fetches: no bilinear sampling and
        // the destination bytes own alpha - never the color opaque-alpha path.
        p.depth=true;p.options.linear=false;p.options.forceOpaqueAlpha=false;
    }
    return p;
}
}
