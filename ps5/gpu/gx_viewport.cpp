// SPDX-License-Identifier: GPL-3.0-only
// Logical mapping derived from the requested WiiCompiled aurora-main/lib/gx/gx.cpp.
// Hardware fields derived from the requested SharpProspero AgcViewport.cs.
#include "gx_viewport.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <stdexcept>

namespace mkw::agc {
namespace {
using aurora::gfx::ClipRect;
using aurora::gfx::Viewport;
struct Candidate {ClipRect rect;int64_t xOffset,yOffset;};
bool finite(const Viewport& v) {
    return std::isfinite(v.left)&&std::isfinite(v.top)&&std::isfinite(v.width)&&
        std::isfinite(v.height)&&std::isfinite(v.znear)&&std::isfinite(v.zfar);
}
double overlap(const Candidate& c,const Viewport& v) {
    const double x0=std::min(double(v.left),double(v.left)+v.width);
    const double x1=std::max(double(v.left),double(v.left)+v.width);
    const double y0=std::min(double(v.top),double(v.top)+v.height);
    const double y1=std::max(double(v.top),double(v.top)+v.height);
    const double left=std::clamp(double(c.rect.x+c.xOffset),x0,x1);
    const double right=std::clamp(double(c.rect.x+c.xOffset+c.rect.width),x0,x1);
    const double top=std::clamp(double(c.rect.y+c.yOffset),y0,y1);
    const double bottom=std::clamp(double(c.rect.y+c.yOffset+c.rect.height),y0,y1);
    // Aurora ranks integer pixel areas. Keep that tie behavior without an
    // overflowing conversion to int on malformed coordinates.
    return std::floor(std::max(right-left,0.0)*std::max(bottom-top,0.0));
}
bool valid_extent(GxRenderExtent e) {
    return e.logicalWidth&&e.logicalHeight&&e.targetWidth&&e.targetHeight&&
        e.logicalWidth<=32767&&e.logicalHeight<=32767&&e.targetWidth<=32767&&e.targetHeight<=32767;
}
}
GxMappedViewport map_gx_viewport(const aurora::gx::GXRegisterState& s,GxRenderExtent e) noexcept {
    const auto& v=s.logicalViewport;const auto& r=s.logicalScissor;
    if(!valid_extent(e)||!finite(v))return {v,{0,0,0,0}};
    if(s.viewportPolicy==AURORA_VIEWPORT_NATIVE)return {v,r};
    Candidate best{{0,0,0,0},0,0};bool found=false;
    double bestOverlap=-1;int64_t bestArea=-1;
    if(r.width>0&&r.height>0)for(int x=-4096;x<=4096;x+=1024)for(int y=-4096;y<=4096;y+=1024) {
        int64_t ox=int64_t(s.scissorOffsetX)+x,oy=int64_t(s.scissorOffsetY)+y;
        int64_t left=std::clamp(int64_t(r.x)-ox,int64_t(0),int64_t(e.logicalWidth));
        int64_t right=std::clamp(int64_t(r.x)+r.width-ox,int64_t(0),int64_t(e.logicalWidth));
        int64_t top=std::clamp(int64_t(r.y)-oy,int64_t(0),int64_t(e.logicalHeight));
        int64_t bottom=std::clamp(int64_t(r.y)+r.height-oy,int64_t(0),int64_t(e.logicalHeight));
        if(left>=right||top>=bottom)continue;
        Candidate c{{int32_t(left),int32_t(top),int32_t(right-left),int32_t(bottom-top)},ox,oy};
        double score=overlap(c,v);int64_t area=(right-left)*(bottom-top);
        if(!found||score>bestOverlap||(score==bestOverlap&&area>bestArea)){
            found=true;best=c;bestOverlap=score;bestArea=area;
        }
    }
    float sx=float(e.targetWidth)/float(e.logicalWidth),sy=float(e.targetHeight)/float(e.logicalHeight);
    Viewport mapped{(v.left-float(best.xOffset))*sx,(v.top-float(best.yOffset))*sy,
        v.width*sx,v.height*sy,v.znear,v.zfar};
    // A zero-area scissor expresses an empty draw directly. Aurora's old
    // offscreen sentinel (1000,1000,1,1) could become visible on larger EFBs.
    if(!found)return {mapped,{0,0,0,0}};
    auto scale=[](int64_t value,uint32_t target,uint32_t logical,bool upper){
        double n=double(value)*target/logical;
        n=upper?std::ceil(n):std::floor(n);
        return int32_t(std::clamp(n,0.0,double(target)));
    };
    int32_t left=scale(best.rect.x,e.targetWidth,e.logicalWidth,false);
    int32_t top=scale(best.rect.y,e.targetHeight,e.logicalHeight,false);
    int32_t right=scale(int64_t(best.rect.x)+best.rect.width,e.targetWidth,e.logicalWidth,true);
    int32_t bottom=scale(int64_t(best.rect.y)+best.rect.height,e.targetHeight,e.logicalHeight,true);
    return {mapped,{left,top,right-left,bottom-top}};
}
std::array<GxContextRegister,14> snapshot_gx_viewport(const GxMappedViewport& m) {
    const auto& v=m.viewport;const auto& s=m.scissor;
    if(!finite(v)||v.width<0||v.height<0||v.znear<0||v.znear>1||v.zfar<0||v.zfar>1)
        throw std::invalid_argument("GX viewport is not a finite AGC viewport");
    float xs=v.width*.5f,ys=-v.height*.5f;
    std::array<float,12> values{xs,v.left+xs,ys,v.top+v.height*.5f,v.zfar-v.znear,v.znear,
        std::min(v.znear,v.zfar),std::max(v.znear,v.zfar),1,1,1,1};
    constexpr uint16_t offsets[12]={0x10f,0x110,0x111,0x112,0x113,0x114,0xb4,0xb5,0x2fa,0x2fb,0x2fc,0x2fd};
    std::array<GxContextRegister,14> out{};
    for(unsigned i=0;i<12;++i){if(!std::isfinite(values[i]))throw std::invalid_argument("GX viewport transform overflow");
        out[i]={offsets[i],0,std::bit_cast<uint32_t>(values[i])};}
    auto corner=[](int64_t x,int64_t y){return uint32_t(std::clamp(x,int64_t(0),int64_t(32767)))|
        (uint32_t(std::clamp(y,int64_t(0),int64_t(32767)))<<16);};
    // Avoid turning negative widths or heights into an inverted rectangle.
    out[12]={0x90,0,corner(s.x,s.y)|0x80000000u};
    out[13]={0x91,0,corner(int64_t(s.x)+std::max(s.width,0),int64_t(s.y)+std::max(s.height,0))};
    return out;
}
}
