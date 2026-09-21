// SPDX-License-Identifier: GPL-3.0-only
#include "gx_blend_state.h"
#include "gx/register_backend.hpp"
#include "gx/command_processor.hpp"
#include "gx/fifo.hpp"
#include "dolphin/gx/frontend.hpp"
#include "dolphin/gx/GXPixel.h"
#include "dolphin/gx/__gx.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <exception>
extern "C" void mkw_diagnostic_log(const char*);
namespace {
struct Settings {GXBlendMode mode=GX_BM_BLEND;GXBlendFactor src=GX_BL_SRCALPHA,dst=GX_BL_INVSRCALPHA;
    GXPixelFmt format=GX_PF_RGBA6_Z24;bool color=true,alpha=true;unsigned constant=UINT32_MAX;};
Settings settings(unsigned frame) {
    Settings c;
    if(frame<64){c.src=GXBlendFactor(frame/8);c.dst=GXBlendFactor(frame%8);}
    else switch(frame){
    case 64:c.mode=GX_BM_NONE;break;
    case 65:c.mode=GX_BM_SUBTRACT;break;
    case 66:c.alpha=false;break;
    case 67:c.color=false;break;
    case 68:c.color=c.alpha=false;break;
    case 69:c.format=GX_PF_RGB8_Z24;c.src=GX_BL_DSTALPHA;c.dst=GX_BL_INVDSTALPHA;break;
    case 70:c.constant=64;break;
    case 71:c.constant=192;break;
    }
    return c;
}
double multiplier(unsigned factor,bool destination,unsigned component,const double* src,const double* dst) {
    switch(factor){case 0:return 0;case 1:return 1;
    case 2:return destination?src[component]:dst[component];
    case 3:return 1-(destination?src[component]:dst[component]);
    case 4:return src[3];case 5:return 1-src[3];case 6:return dst[3];default:return 1-dst[3];}
}
}
extern "C" int mkw_blend_frame(unsigned frame,unsigned defaults,unsigned* control,unsigned* mask,float* constant) {
    if(frame>=72)return 1;
    try {
        auto c=settings(frame);
        if(frame==0) {
            // The isolated diagnostic does not run GXInit. Reproduce its BP
            // address-byte initialization from GXManage.cpp before setters.
            __gx->cmode0=(__gx->cmode0&0xffffffu)|0x41000000u;
            __gx->cmode1=(__gx->cmode1&0xffffffu)|0x42000000u;
            __gx->peCtrl=(__gx->peCtrl&0xffffffu)|0x43000000u;
        }
        GXSetPixelFmt(c.format,GX_ZC_LINEAR);GXSetBlendMode(c.mode,c.src,c.dst,GX_LO_COPY);
        GXSetColorUpdate(c.color);GXSetAlphaUpdate(c.alpha);GXSetDstAlpha(c.constant!=UINT32_MAX,uint8_t(c.constant));
        aurora::gx::fifo::drain();
        const auto& decoded=aurora::gx::register_state();
        if(decoded.blendMode!=c.mode || decoded.blendFacSrc!=c.src || decoded.blendFacDst!=c.dst ||
           decoded.pixelFmt!=c.format || decoded.colorUpdate!=c.color || decoded.alphaUpdate!=c.alpha || decoded.dstAlpha!=c.constant)
            throw std::runtime_error("GX producer/decoder blend state differs from request");
        auto snapshot=mkw::agc::snapshot_gx_blend(decoded,defaults);
        *control=snapshot.control;*mask=snapshot.targetMask;std::memcpy(constant,snapshot.constant.data(),16);
        return 0;
    }catch(const std::exception& e){mkw_diagnostic_log("[mkw-blend] FAIL ");mkw_diagnostic_log(e.what());mkw_diagnostic_log("\n");return 2;}
}
extern "C" int mkw_blend_expected(unsigned frame,unsigned pixel,unsigned* expected,unsigned* writeMask) {
    if(frame>=72 || pixel>=20)return 1;
    *expected=0x80201828u;*writeMask=0;if(pixel>=16)return 0;
    auto c=settings(frame);const bool right=pixel%4>=2,bottom=pixel/4>=2;
    const unsigned sourceBytes[4]={unsigned(right==bottom?128:0),unsigned(right?63:0),unsigned(bottom?63:0),unsigned(bottom?219:109)};
    const unsigned destBytes[4]={32,24,40,128};double s[4],d[4];
    for(unsigned i=0;i<4;++i){s[i]=sourceBytes[i]/255.0;d[i]=destBytes[i]/255.0;}
    const bool hasAlpha=c.format==GX_PF_RGBA6_Z24;
    *writeMask=(c.color?7u:0u)|((hasAlpha&&c.alpha)?8u:0u);
    unsigned src=c.src,dst=c.dst;
    if(!hasAlpha){if(src==6)src=1;else if(src==7)src=0;if(dst==6)dst=1;else if(dst==7)dst=0;}
    unsigned bytes[4];
    for(unsigned i=0;i<4;++i){
        if(!(*writeMask&(1u<<i))){bytes[i]=destBytes[i];continue;}
        double value=s[i];
        if(i==3 && c.constant!=UINT32_MAX)value=s[3]*(c.constant/255.0);
        else if(c.mode==GX_BM_SUBTRACT)value=d[i]-s[i];
        else if(c.mode==GX_BM_BLEND)value=s[i]*multiplier(src,false,i,s,d)+d[i]*multiplier(dst,true,i,s,d);
        bytes[i]=unsigned(std::lround(std::clamp(value,0.0,1.0)*255.0));
    }
    *expected=(bytes[3]<<24)|(bytes[0]<<16)|(bytes[1]<<8)|bytes[2];return 0;
}
