// SPDX-License-Identifier: GPL-3.0-only
#include "gx_sampler.h"
#include <algorithm>
#include <stdexcept>

namespace mkw::agc {
static unsigned address(GXTexWrapMode mode) {
    switch(mode) {
    case GX_CLAMP: return 2;
    case GX_REPEAT: return 0;
    case GX_MIRROR: return 1;
    default: throw std::invalid_argument("Invalid GX sampler wrap mode");
    }
}
std::array<uint32_t,4> gx_sampler(const GXTexObj_& texture,bool arbitraryMips,uint16_t configuredAnisotropy) {
    if(configuredAnisotropy==0 || configuredAnisotropy>16 ||
       (configuredAnisotropy&(configuredAnisotropy-1)))
        throw std::invalid_argument("AGC anisotropy must be 1, 2, 4, 8 or 16");
    unsigned min=0,mip=0,mag=texture.mag_filter()==GX_LINEAR;
    switch(texture.min_filter()) {
    case GX_NEAR: break;
    case GX_LINEAR: min=1; break;
    case GX_NEAR_MIP_NEAR: mip=1; break;
    case GX_LIN_MIP_NEAR: min=1;mip=1;break;
    case GX_NEAR_MIP_LIN: mip=2;break;
    case GX_LIN_MIP_LIN: min=1;mip=2;break;
    default: throw std::invalid_argument("Invalid GX sampler filter");
    }
    if(arbitraryMips && mip) mip=2;
    float lo=mip?texture.min_lod():0.0f,hi=mip?texture.max_lod():0.0f;
    if(lo>hi) throw std::invalid_argument("Inverted GX sampler LOD range");
    unsigned aniso=1;
    switch(texture.max_aniso()) {
    case GX_ANISO_1: case GX_MAX_ANISOTROPY: break;
    case GX_ANISO_2: aniso=std::max<unsigned>(configuredAnisotropy/2,1);break;
    case GX_ANISO_4: aniso=configuredAnisotropy;break;
    default: throw std::invalid_argument("Invalid GX anisotropy");
    }
    if(!(mag && min && mip==2)) aniso=1;
    unsigned ratio=0; for(unsigned a=aniso;a>1;a>>=1) ++ratio;
    if(ratio) {mag=3;min=3;}
    // GX's unsigned 4.4 LOD values convert exactly to AGC unsigned 4.8.
    // Retain SharpProspero Create()'s point Z filter and zero border color.
    return {address(texture.wrap_s())|(address(texture.wrap_t())<<3)|(ratio<<9),
            unsigned(lo*256.0f)|(unsigned(hi*256.0f)<<12),
            (mag<<20)|(min<<22)|(1u<<24)|(mip<<26),0};
}
}
