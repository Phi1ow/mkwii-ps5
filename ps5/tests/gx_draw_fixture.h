// SPDX-License-Identifier: GPL-3.0-only
// Diagnostic geometry/state shared by host orchestration and native GPU tests.
#pragma once
#include "gx_draw_packet.h"
#include <bit>
namespace mkw::test {
inline aurora::gx::GXRegisterState draw_state(unsigned w,unsigned h){
    aurora::gx::GXRegisterState s{};s.vtxDesc.fill(GX_NONE);
    s.vtxDesc[GX_VA_POS]=GX_DIRECT;s.vtxFmts[0].attrs[GX_VA_POS]={GX_POS_XYZ,GX_F32,0,0};
    s.numTevStages=1;s.numTexGens=0;s.numIndStages=0;
    for(auto& c:s.colorRegs)c={0,0,0,0};for(auto& c:s.kcolors)c={0,0,0,0};
    s.colorRegs[0]={192.f/255,64.f/255,32.f/255,1};
    auto& t=s.tevStages[0];t.texMapId=GX_TEXMAP_NULL;t.texCoordId=GX_TEXCOORD_NULL;
    t.colorPass={GX_CC_ZERO,GX_CC_ZERO,GX_CC_ZERO,GX_CC_CPREV};
    t.alphaPass={GX_CA_ZERO,GX_CA_ZERO,GX_CA_ZERO,GX_CA_APREV};
    s.renderViewport=s.logicalViewport={0,0,float(w),float(h),0,1};
    s.renderScissor=s.logicalScissor={0,0,int(w),int(h)};
    s.proj={{1,0,0,0},{0,1,0,0},{0,0,1,0},{0,0,0,1}};
    s.pnMtx[0].pos={{1,0,0,0},{0,1,0,0},{0,0,1,0}};
    s.pnMtx[0].nrm={{1,0,0},{0,1,0},{0,0,1}};
    s.cullMode=GX_CULL_NONE;s.depthCompare=true;s.depthFunc=GX_LESS;s.depthUpdate=true;
    s.pixelFmt=GX_PF_RGBA6_Z24;s.dstAlpha=UINT32_MAX;s.colorUpdate=s.alphaUpdate=true;
    return s;
}
inline mkw::agc::GxGeometry draw_geometry(const aurora::gx::GXRegisterState& s,float storedDepth=.75f,bool reverse=false){
    std::vector<uint8_t> bytes;
    constexpr float xy[4][2]={{-.75f,.75f},{.75f,.75f},{.75f,-.75f},{-.75f,-.75f}};
    for(unsigned n=0;n<4;++n){unsigned i=reverse?(4-n)%4:n;
        for(float f:{xy[i][0],xy[i][1],-storedDepth}){uint32_t u=std::bit_cast<uint32_t>(f);for(int b=3;b>=0;--b)bytes.push_back(uint8_t(u>>(8*b)));}}
    return mkw::agc::GxGeometry::snapshot(s,GX_QUADS,GX_VTXFMT0,4,bytes,nullptr,nullptr);
}
}
