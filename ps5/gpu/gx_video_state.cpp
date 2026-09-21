// SPDX-License-Identifier: GPL-3.0-only
#include "gx_video_state.h"
#include <aurora/render_size_limits.hpp>
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace mkw::agc {
GxVideoPlan plan_gx_video(const GxVideoSettings& s){
    if(!s.surfaceWidth||!s.surfaceHeight||s.surfaceWidth>16384||s.surfaceHeight>16384||
       !std::isfinite(s.scale)||s.scale<0||s.scale>64||unsigned(s.policy)>AURORA_VIEWPORT_NATIVE)
        throw std::invalid_argument("Invalid VI render configuration");
    GxVideoPlan p{};p.policy=s.policy;
    auto& e=p.extent;e.logicalWidth=640;e.logicalHeight=528;
    if(s.mode){
        e.logicalWidth=std::max<uint32_t>(640,s.mode->fbWidth);e.logicalHeight=std::max<uint32_t>(528,s.mode->efbHeight);
        if(e.logicalWidth>16384||e.logicalHeight>16384)throw std::invalid_argument("VI EFB extent exceeds native renderer limits");
        if(s.mode->viWidth&&s.mode->viHeight){
            const auto format=(s.mode->viTVmode>>2)&7;const bool pal=format==VI_PAL||format==VI_DEBUG_PAL;
            const float activeW=pal?864.f*(52.f/64.f):858.f*(52.655555f/63.555555f);
            const float activeH=pal?576.f:486.f;
            p.aspectCorrection=(float(s.mode->viWidth)/activeW)/(float(s.mode->viHeight)/activeH);
        }
    }
    e.targetWidth=s.surfaceWidth;e.targetHeight=s.surfaceHeight;
    if(s.policy==AURORA_VIEWPORT_FIT){
        float scale=s.scale>0?s.scale:std::max(1.f,std::min(float(s.surfaceWidth)/e.logicalWidth,float(s.surfaceHeight)/e.logicalHeight));
        e.targetWidth=std::max(1u,uint32_t(std::lround(float(e.logicalWidth)*scale)));
        e.targetHeight=std::max(1u,uint32_t(std::lround(float(e.logicalHeight)*scale)));
    }else if(s.scale>0){
        const auto size=aurora::render_size_limits::scale_framebuffer_to_aspect(e.logicalWidth,e.logicalHeight,s.scale,float(s.surfaceWidth)/s.surfaceHeight);
        e.targetWidth=size.width;e.targetHeight=size.height;
    }
    auto size=aurora::render_size_limits::fit_framebuffer_to_budget(e.targetWidth,e.targetHeight,16384);
    e.targetWidth=size.width;e.targetHeight=size.height;
    if(s.policy==AURORA_VIEWPORT_STRETCH)p.presentAspect=float(s.surfaceWidth)/s.surfaceHeight;
    else if(s.aspectWidth>0&&s.aspectHeight>0)p.presentAspect=(float(s.aspectWidth)/s.aspectHeight)*p.aspectCorrection;
    return p;
}
}
