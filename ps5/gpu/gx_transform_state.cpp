// SPDX-License-Identifier: GPL-3.0-only
#include "gx_transform_state.h"
#include <algorithm>
#include <cstring>
namespace mkw::agc {
GxTransformState GxTransformState::snapshot(const aurora::gx::GXRegisterState& s) {
    namespace gx=aurora::gx;
    static_assert(gx::MaxPostexMtx==20 && gx::MaxPnMtx==10 && gx::MaxPTTexMtx==20);
    GxTransformState out;
    auto projection=s.proj;
    // Aurora shader_info.cpp effective_projection(): apply once, before upload.
    const bool flip=(s.renderViewport.znear<=s.renderViewport.zfar)==gx::UseReversedZ;
    for(unsigned i=0;i<4;++i)projection.m2[i]=flip?-projection.m2[i]:projection.m2[i]+projection.m3[i];
    auto copy=[&](unsigned offset,const auto& matrix){std::memcpy(out.values.data()+offset/4,&matrix,sizeof(matrix));};
    copy(MKW_GX_TRANSFORM_PROJECTION,projection);
    auto* vp=out.values.data()+MKW_GX_TRANSFORM_VIEWPORT/4;
    vp[0]=-1.f/(6.f*std::max(std::abs(s.renderViewport.width),1.f));
    vp[1]=1.f/(6.f*std::max(std::abs(s.renderViewport.height),1.f));
    vp[2]=s.renderViewport.width;vp[3]=s.renderViewport.height;
    for(unsigned i=0;i<gx::MaxPostexMtx;++i)
        copy(MKW_GX_TRANSFORM_POSTEX+i*48,i<gx::MaxPnMtx?s.pnMtx[i].pos:s.texMtxs[i-gx::MaxPnMtx]);
    for(unsigned i=0;i<gx::MaxPnMtx;++i)copy(MKW_GX_TRANSFORM_NORMAL+i*48,s.pnMtx[i].nrm);
    for(unsigned i=0;i<gx::MaxPTTexMtx;++i)copy(MKW_GX_TRANSFORM_POSTTEXTURE+i*48,s.ptTexMtxs[i]);
    return out;
}
}
