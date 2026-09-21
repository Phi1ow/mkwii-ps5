// SPDX-License-Identifier: GPL-3.0-only
#include "agc_blit.h"
#include <stdexcept>
#include <cmath>
namespace mkw::agc {
static void validate(uint32_t width,uint32_t height,BlitRect r) {
    if(!width||!height||width>16384||height>16384||!r.width||!r.height||
        r.x>=width||r.y>=height||r.width>width-r.x||r.height>height-r.y)
        throw std::invalid_argument("Blit rectangle outside image");
}
std::array<BlitVertex,4> blit_vertices(uint32_t sw,uint32_t sh,uint32_t dw,uint32_t dh,BlitRect s,BlitRect d) {
    validate(sw,sh,s);validate(dw,dh,d);
    return sampled_blit_vertices(sw,sh,dw,dh,{float(s.x),float(s.y),float(s.width),float(s.height)},d);
}
std::array<BlitVertex,4> sampled_blit_vertices(uint32_t sw,uint32_t sh,uint32_t dw,uint32_t dh,BlitSampleRect s,BlitRect d) {
    validate(dw,dh,d);
    if(!sw||!sh||sw>16384||sh>16384||!std::isfinite(s.x)||!std::isfinite(s.y)||
       !std::isfinite(s.width)||!std::isfinite(s.height)||s.x<0||s.y<0||s.width<=0||s.height<=0||
       s.x>=sw||s.y>=sh||s.width>sw-s.x||s.height>sh-s.y)
        throw std::invalid_argument("Sampled blit rectangle outside image");
    const float x[2]={2.f*d.x/dw-1.f,2.f*(d.x+d.width)/dw-1.f};
    const float y[2]={1.f-2.f*d.y/dh,1.f-2.f*(d.y+d.height)/dh};
    const float u[2]={float(s.x)/sw,float(s.x+s.width)/sw};
    const float v[2]={float(s.y)/sh,float(s.y+s.height)/sh};
    std::array<BlitVertex,4> out{};
    for(unsigned i=0;i<4;++i){unsigned right=i==1||i==2,bottom=i>=2;
        out[i]={{x[right],y[bottom],.5f},{u[right],v[bottom],0},{0,0},0xffffffff};}
    return out;
}
}
