// SPDX-License-Identifier: GPL-3.0-only
// Packing follows WiiCompiled shader_info.cpp fog_uniform(); the range width
// is expressed in render-target pixels to match the exported NDC x position.
#include "gx_fog_state.h"
#include <cmath>
#include <cstddef>
#include <stdexcept>
namespace mkw::agc {
static_assert(sizeof(MkwFog)==MKW_GX_FOG_BYTES);
static_assert(offsetof(MkwFog,abc)==16&&offsetof(MkwFog,rangeBase)==32&&offsetof(MkwFog,zmap)==48);
static_assert(offsetof(MkwFog,rangeK)==64&&offsetof(MkwFog,ctl)==112);
static_assert(GX_FOG_NONE==0&&GX_FOG_PERSP_LIN==2&&GX_FOG_ORTHO_LIN==10);
static_assert(GX_FOG_PERSP_EXP==4&&GX_FOG_PERSP_EXP2==5&&GX_FOG_PERSP_REVEXP==6&&GX_FOG_PERSP_REVEXP2==7);
static_assert(GX_FOG_ORTHO_EXP==12&&GX_FOG_ORTHO_EXP2==13&&GX_FOG_ORTHO_REVEXP==14&&GX_FOG_ORTHO_REVEXP2==15);
MkwFog snapshot_gx_fog(const aurora::gx::GXRegisterState& s){
    MkwFog f{};
    const unsigned type=unsigned(s.fog.type);
    if(!type)return f;
    // The reference switch accepts the undocumented linear ids 1/3/9/11 and
    // fatals on anything else (0/8 or >15).
    static const bool supported[16]={false,true,true,true,true,true,true,true,
        false,true,true,true,true,true,true,true};
    if(type>=16||!supported[type])throw std::invalid_argument("Unsupported GX fog type");
    if(!std::isfinite(s.fog.aRaw)||!std::isfinite(s.fog.c)||s.fog.bShift>31||s.fog.bMagnitude>0xffffffu)
        throw std::invalid_argument("Invalid GX fog parameters");
    for(unsigned i=0;i<4;++i){
        if(!std::isfinite(s.fog.color[i]))throw std::invalid_argument("Nonfinite GX fog color");
        f.color[i]=s.fog.color[i]*255.f;
    }
    f.abc[0]=s.fog.aRaw;f.abc[1]=float(s.fog.bMagnitude);f.abc[2]=s.fog.c;f.abc[3]=float(s.fog.bShift);
    const uint32_t rangeRegister=s.fogRange[0];
    const bool rangeEnabled=(rangeRegister&(1u<<10))!=0;
    float rangeWidth=std::fabs(s.xfViewport[0])*2.f;
    if(!std::isfinite(rangeWidth)||rangeWidth<=0)rangeWidth=1.f;
    const auto& rv=s.renderViewport;
    if(!std::isfinite(rv.width)||rv.width<=0||!std::isfinite(rv.left)||
        !std::isfinite(rv.znear)||!std::isfinite(rv.zfar))
        throw std::invalid_argument("Invalid GX fog render viewport");
    const int rangeCenter=int(rangeRegister&0x3ffu)-342;
    const float screenSpaceCenter=rangeEnabled?(float(rangeCenter)/rangeWidth)*2.f-1.f:0.f;
    // rangeBase.y is divided into a target-pixel x in the shader, so it carries
    // the mapped viewport width rather than Aurora's logical rangeWidth.
    f.rangeBase[0]=screenSpaceCenter;
    f.rangeBase[1]=rangeEnabled?rv.width:1.f;
    f.rangeBase[2]=rv.width*.5f;f.rangeBase[3]=rv.left+rv.width*.5f;
    f.zmap[0]=rv.zfar-rv.znear;f.zmap[1]=rv.znear;
    unsigned k=0;
    for(unsigned i=1;i<6;++i){
        const uint32_t packed=s.fogRange[i];
        f.rangeK[k++]=float((packed>>12)&0xfffu)/64.f;
        f.rangeK[k++]=float(packed&0xfffu)/64.f;
    }
    f.rangeK[10]=f.rangeK[11]=f.rangeK[9];
    f.ctl[0]=type;f.ctl[1]=rangeEnabled?1u:0u;
    return f;
}
}
