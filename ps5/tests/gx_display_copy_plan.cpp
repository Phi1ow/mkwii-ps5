// SPDX-License-Identifier: GPL-3.0-only
#include "gx_display_copy.h"
#include <cstdio>
#include <cmath>
#include <stdexcept>
using namespace mkw::agc;
int main(){try{
    unsigned checks=0;auto check=[&](bool b){++checks;if(!b)throw std::runtime_error("Display copy plan mismatch");};
    auto rejects=[&](auto f){bool failed=false;try{f();}catch(const std::exception&){failed=true;}check(failed);};
    aurora::gx::GXRegisterState s{};s.pixelFmt=GX_PF_RGBA6_Z24;
    s.dispCopySrc={1,1,5,3};s.dispCopyDstWidth=5;s.dispCopyDstHeight=3;
    s.texCopySrc={100,100,1,1};s.texCopyDstWidth=17;s.texCopyDstHeight=18;s.texCopyFmt=GX_CTF_RA8;
    s.texCopySrcRenderSpace=false;s.texCopyHalfScale=true;s.dstAlpha=80;
    GxRenderExtent e{640,528,960,792};
    auto p=plan_gx_display_copy(s,e,false);
    check(p.source.x==1&&p.source.y==1&&p.source.width==8&&p.source.height==5);
    check(p.logicalWidth==5&&p.logicalHeight==3&&p.scaledWidth==8&&p.scaledHeight==5);
    check(p.options.format==GX_TF_RGBA8&&!p.options.forceOpaqueAlpha&&p.preCopyAlpha==UINT32_MAX);
    check(std::abs(p.options.rowStride-5.f/3)<.00001f&&!p.clearMask&&!p.clearDepth);
    s.dispCopyDstWidth=s.dispCopyDstHeight=0;p=plan_gx_display_copy(s,e,false);check(p.logicalWidth==5&&p.logicalHeight==3);
    s.copyFilterVf=true;s.copyFilterVFilter={8,8,10,12,10,8,8};s.copyClamp=GX_CLAMP_TOP;
    p=plan_gx_display_copy(s,e,false);check((p.options.filter==std::array<uint32_t,3>{16,32,16})&&!p.options.linear);
    check(p.options.clampTop&&!p.options.clampBottom);
    p=plan_gx_display_copy(s,e,false,true);check((p.options.filter==std::array<uint32_t,3>{0,64,0}));
    s.copyFilterVFilter.fill(63);p=plan_gx_display_copy(s,e,false,true);check((p.options.filter==std::array<uint32_t,3>{0,441,0}));
    s.copyFilterVf=false;s.clearColor={.25f,.5f,.75f,1};s.clearDepth=0x400000;
    for(bool rgb:{false,true})for(unsigned mask=0;mask<8;++mask){
        s.pixelFmt=rgb?GX_PF_RGB8_Z24:GX_PF_RGBA6_Z24;
        s.colorUpdate=mask&1;s.alphaUpdate=mask&2;s.depthUpdate=mask&4;
        p=plan_gx_display_copy(s,e,true);
        check(p.clearMask==((mask&1?7u:0u)|(!rgb&&(mask&2)?8u:0u))&&p.clearDepth==bool(mask&4));
        check(p.clearRect.x==1&&p.clearRect.y==1&&p.clearRect.width==8&&p.clearRect.height==5);
        if(mask&4)check(p.depthClearValue==.75f);
        check(!p.options.forceOpaqueAlpha&&p.preCopyAlpha==UINT32_MAX);
    }
    s.dispCopySrc={638,526,5,3};p=plan_gx_display_copy(s,e,false);check(p.source.width==3&&p.source.height==3);
    s.dispCopySrc={0,0,640,528};p=plan_gx_display_copy(s,{640,528,320,264},false);check(p.options.rowStride==1);
    s.viewportPolicy=AURORA_VIEWPORT_NATIVE;s.dispCopySrc={1,1,5,3};p=plan_gx_display_copy(s,e,false);
    check(p.source.x==1&&p.source.width==5&&p.scaledWidth==5&&p.scaledHeight==3&&p.options.rowStride==1);
    s.copyFilterAa=true;rejects([&]{plan_gx_display_copy(s,e,false);});s.copyFilterAa=false;
    s.dispCopySrc.width=0;rejects([&]{plan_gx_display_copy(s,e,false);});s.dispCopySrc={960,0,1,1};rejects([&]{plan_gx_display_copy(s,e,false);});
    rejects([&]{plan_gx_display_copy(s,{0,1,1,1},false);});
    std::printf("PASS %u display-copy plan checks: rounding/clipping, dimensions, filter, independent clear masks and native viewport\n",checks);return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL %s\n",e.what());return 1;}}
