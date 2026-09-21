// SPDX-License-Identifier: GPL-3.0-only
#include "gx_lighting_state.h"
#include <bit>
#include <stdexcept>
namespace mkw::agc {
static_assert(GX_AF_SPEC==0 && GX_AF_SPOT==1 && GX_AF_NONE==2);
static_assert(GX_DF_NONE==0 && GX_DF_SIGN==1 && GX_DF_CLAMP==2);
static_assert(GX_SRC_REG==0 && GX_SRC_VTX==1 && GX_ALPHA0==2);
static float sanitize(float value) {
    uint32_t bits=std::bit_cast<uint32_t>(value);
    if((bits&0x7f800000u)!=0x7f800000u)return value;
    if(bits&0x007fffffu)return 0;
    return bits&0x80000000u?-1.f:1.f;
}
MkwLighting snapshot_gx_lighting(const aurora::gx::GXRegisterState& s) {
    MkwLighting out{};
    auto vec=[](const auto& v){return MkwLightVec{v[0],v[1],v[2],v[3]};};
    for(unsigned i=0;i<8;++i) {
        const auto& v=s.lights[i];auto& light=out.lights[i];
        light={vec(v.pos),vec(v.dir),vec(v.color),vec(v.cosAtt),vec(v.distAtt)};
        // Same preparation as Aurora prepare_shader_light; do not trust the
        // mutable preparedLightsDirty cache when capturing an independent draw.
        if(std::abs(light.dist_att.x)<1e-5f && std::abs(light.dist_att.y)<1e-5f && std::abs(light.dist_att.z)<1e-5f)light.dist_att.x=1e-5f;
        double n=double(light.dir.x)*light.dir.x+double(light.dir.y)*light.dir.y+double(light.dir.z)*light.dir.z;
        if(n==0){light.dir.x=light.dir.y=light.dir.z=0;}
        else {double inverse=1/std::sqrt(n);light.dir.x=sanitize(float(light.dir.x*inverse));light.dir.y=sanitize(float(light.dir.y*inverse));light.dir.z=sanitize(float(light.dir.z*inverse));}
    }
    for(unsigned i=0;i<4;++i) {
        const auto& c=s.colorChannelConfig[i];const auto& v=s.colorChannelState[i];
        if(unsigned(c.matSrc)>1 || unsigned(c.ambSrc)>1 || unsigned(c.diffFn)>2 || unsigned(c.attnFn)>2)
            throw std::invalid_argument("Invalid GX lighting channel enum");
        out.channels[i]={unsigned(c.lightingEnabled),unsigned(c.matSrc),unsigned(c.ambSrc),unsigned(v.lightMask.to_ulong()),
            unsigned(c.diffFn),unsigned(c.attnFn),0,0,vec(v.matColor),vec(v.ambColor)};
    }
    return out;
}
}
