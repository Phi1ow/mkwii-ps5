// SPDX-License-Identifier: GPL-3.0-only
#include "gx_texture_copy.h"
#include <cmath>
#include <cstdio>
#include <limits>
#include <stdexcept>
using namespace mkw::agc;
int main(){try{
    unsigned checks=0;auto check=[&](bool b){++checks;if(!b)throw std::runtime_error("GX texture copy plan regression");};
    auto near=[&](float a,float b){check(std::abs(a-b)<.001f);};
    auto rejects=[&](auto fn){bool failed=false;try{fn();}catch(const std::exception&){failed=true;}check(failed);};
    aurora::gx::GXRegisterState s{};s.dstAlpha=UINT32_MAX;s.pixelFmt=GX_PF_RGBA6_Z24;
    s.texCopySrc={80,45,480,270};s.texCopyDstWidth=640;s.texCopyDstHeight=360;s.texCopyFmt=GX_CTF_RA8;
    GxRenderExtent extent{640,360,1920,1080};
    auto p=plan_gx_texture_copy(s,extent,false);
    check(p.logicalWidth==640&&p.logicalHeight==360&&p.scaledWidth==1920&&p.scaledHeight==1080);
    near(p.source.x,240);near(p.source.y,135);near(p.source.width,1440);near(p.source.height,810);
    check(p.options.format==GX_CTF_RA8&&p.options.linear&&!p.options.forceOpaqueAlpha);near(p.options.rowStride,3);
    s.texCopySrc={1,1,5,3};s.texCopyDstWidth=5;s.texCopyDstHeight=3;
    for(auto target: {GxRenderExtent{640,360,1280,720},GxRenderExtent{640,360,960,540},GxRenderExtent{640,360,320,180}}){
        p=plan_gx_texture_copy(s,target,false);const float scale=float(target.targetWidth)/640;
        near(p.source.x,scale);near(p.source.y,scale);near(p.source.width,5*scale);near(p.source.height,3*scale);
        check(p.scaledWidth==uint32_t(std::round(5*scale))&&p.scaledHeight==uint32_t(std::round(3*scale)));
        auto v=sampled_blit_vertices(target.targetWidth,target.targetHeight,p.scaledWidth,p.scaledHeight,
            p.source,{0,0,p.scaledWidth,p.scaledHeight});
        near(v[0].normal[0],1.f/640);near(v[0].normal[1],1.f/360);
        near(v[2].normal[0],6.f/640);near(v[2].normal[1],4.f/360);
        near(p.options.rowStride,scale);
    }
    s.viewportPolicy=AURORA_VIEWPORT_NATIVE;p=plan_gx_texture_copy(s,extent,false);
    check(p.scaledWidth==5&&p.scaledHeight==3&&!p.options.linear);near(p.source.x,1);
    s.viewportPolicy=AURORA_VIEWPORT_FIT;s.texCopySrcRenderSpace=true;p=plan_gx_texture_copy(s,extent,false);
    check(p.scaledWidth==15&&p.options.linear);near(p.source.x,1);
    s.texCopySrcRenderSpace=false;s.copyFilterVf=true;s.copyFilterVFilter={8,8,10,12,10,8,8};
    s.copyClamp=GXFBClamp(GX_CLAMP_TOP|GX_CLAMP_BOTTOM);p=plan_gx_texture_copy(s,extent,false);
    check((p.options.filter==std::array<uint32_t,3>{16,32,16}));
    check(!p.options.linear&&p.options.clampTop&&p.options.clampBottom);
    s.texCopyHalfScale=true;check(plan_gx_texture_copy(s,extent,false).options.linear);
    s.texCopyHalfScale=false;s.pixelFmt=GX_PF_RGB8_Z24;check(plan_gx_texture_copy(s,extent,false).options.forceOpaqueAlpha);
    s.copyFilterVFilter[0]=64;rejects([&]{plan_gx_texture_copy(s,extent,false);});
    s.copyFilterVFilter[0]=8;s.copyFilterAa=GX_TRUE;rejects([&]{plan_gx_texture_copy(s,extent,false);});s.copyFilterAa=GX_FALSE;
    for(auto format:{0,1,2,3,4,5,6,0x20,0x22,0x23,0x27,0x28,0x29,0x2a,0x2b,0x2c}){
        s.texCopyFmt=GXTexFmt(format);check(plan_gx_texture_copy(s,extent,false).options.format==unsigned(format));}
    for(auto format:{GX_TF_Z16,GX_TF_Z24X8}){
        s.texCopyFmt=format;p=plan_gx_texture_copy(s,extent,false);
        check(p.depth&&p.options.format==unsigned(format)&&!p.options.linear&&!p.options.forceOpaqueAlpha);}
    s.texCopyFmt=GX_TF_Z8;rejects([&]{plan_gx_texture_copy(s,extent,false);});
    s.texCopyFmt=GX_CTF_Z16L;rejects([&]{plan_gx_texture_copy(s,extent,false);});
    s.texCopyFmt=GX_CTF_RA8;
    p=plan_gx_texture_copy(s,extent,true);check(p.clearDepth&&p.depthClearValue==1.f/16777216.f);
    check(!plan_gx_texture_copy(s,extent,false).clearDepth);
    for(uint32_t z:{0u,1u,0x400000u,0x800000u,0xfffffeu,0xffffffu,0xffffffffu}){
        s.clearDepth=z;p=plan_gx_texture_copy(s,extent,true);
        check(p.clearDepth&&p.depthClearValue==1.f-float(std::min(z,0xffffffu))/16777216.f);
    }
    s.clearDepth=0x400000;s.colorUpdate=s.alphaUpdate=false;
    p=plan_gx_texture_copy(s,extent,true);check(p.clearDepth&&p.clearMask==0&&p.depthClearValue==.75f);
    s.colorUpdate=s.alphaUpdate=s.depthUpdate=false;check(plan_gx_texture_copy(s,extent,true).scaledWidth==15);
    check(!plan_gx_texture_copy(s,extent,true).clearDepth);
    s.pixelFmt=GX_PF_RGBA6_Z24;s.alphaUpdate=true;s.dstAlpha=80;
    check(plan_gx_texture_copy(s,extent,false).preCopyAlpha==80);
    s.dstAlpha=256;rejects([&]{plan_gx_texture_copy(s,extent,false);});s.dstAlpha=UINT32_MAX;
    s.clearColor={16.f/255,32.f/255,48.f/255,64.f/255};
    for(unsigned bits=0;bits<4;++bits){
        s.colorUpdate=bits&1;s.alphaUpdate=bits&2;p=plan_gx_texture_copy(s,extent,true);
        check(p.clearMask==((bits&1?7:0)|(bits&2?8:0)));
        check(p.clearColor==((bits&1?0x00102030u:0)|(bits&2?0x40000000u:0)));
        check(p.clearRect.x==3&&p.clearRect.y==3&&p.clearRect.width==15&&p.clearRect.height==9);
    }
    s.pixelFmt=GX_PF_RGB8_Z24;check(plan_gx_texture_copy(s,extent,true).clearMask==7);
    s.dstAlpha=80;check(plan_gx_texture_copy(s,extent,true).preCopyAlpha==UINT32_MAX);
    s.dstAlpha=UINT32_MAX;s.pixelFmt=GX_PF_RGBA6_Z24;
    p=plan_gx_texture_copy(s,{640,360,320,180},true);
    check(p.clearRect.x==1&&p.clearRect.y==1&&p.clearRect.width==2&&p.clearRect.height==1);
    s.clearColor.x()=std::numeric_limits<float>::quiet_NaN();rejects([&]{plan_gx_texture_copy(s,extent,true);});
    s.clearColor={0,0,0,1};
    s.texCopySrc.x=-1;rejects([&]{plan_gx_texture_copy(s,extent,false);});s.texCopySrc.x=1;
    // Sources past the EFB edge are clipped like Aurora's resolve_pass (at least one
    // texel kept, filter stride unchanged), including the clear rectangle.
    s.texCopySrc={638,358,5,3};p=plan_gx_texture_copy(s,extent,true);
    near(p.source.x,1914);near(p.source.width,6);near(p.source.y,1074);near(p.source.height,6);near(p.options.rowStride,3);
    check(p.clearRect.x==1914&&p.clearRect.width==6&&p.clearRect.y==1074&&p.clearRect.height==6);
    s.texCopySrc={640,360,5,3};p=plan_gx_texture_copy(s,extent,true);
    near(p.source.x,1919);near(p.source.width,1);near(p.source.y,1079);near(p.source.height,1);
    check(p.clearRect.x==1919&&p.clearRect.width==1&&p.clearRect.y==1079&&p.clearRect.height==1);
    (void)sampled_blit_vertices(1920,1080,p.scaledWidth,p.scaledHeight,p.source,{0,0,p.scaledWidth,p.scaledHeight});
    s.texCopySrc={1,1,5,3};
    rejects([&]{plan_gx_texture_copy(s,{0,360,1920,1080},false);});
    rejects([&]{sampled_blit_vertices(10,10,10,10,{0,0,std::numeric_limits<float>::infinity(),1},{0,0,10,10});});
    rejects([&]{sampled_blit_vertices(10,10,10,10,{.5f,0,10,1},{0,0,10,10});});
    std::printf("PASS %u GX texture-copy plan checks: fractional mapping, scales, filters, 16 color formats plus Z16/Z24X8 depth copies, reversed-Z depth clears and explicit unsupported-state rejection\n",checks);
    return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL %s\n",e.what());return 1;}}
