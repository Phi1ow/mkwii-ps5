// SPDX-License-Identifier: GPL-3.0-only
#include "gx_texgen_state.h"
#include <stdexcept>
namespace mkw::agc {
static_assert(GX_TG_MTX3x4==0 && GX_TG_MTX2x4==1 && GX_TG_SRTG==10);
static_assert(GX_TG_POS==0 && GX_TG_NRM==1 && GX_TG_BINRM==2 && GX_TG_TANGENT==3);
static_assert(GX_TG_TEX0==4 && GX_TG_TEX7==11 && GX_TG_COLOR0==19 && GX_TG_COLOR1==20 && GX_MAX_TEXGENSRC==21);
MkwTexgenState snapshot_gx_texgen(const aurora::gx::GXRegisterState& s,unsigned used) {
    if(s.numTexGens>8 || used>255 || (used>>s.numTexGens))throw std::invalid_argument("GX texgen use outside active coordinate count");
    MkwTexgenState out{};out.count=s.numTexGens;out.used_mask=used;
    for(unsigned i=0;i<8;++i) {
        auto& d=out.generators[i];d.matrix=d.post_matrix=~0u;
        if(!(used&(1u<<i)))continue;
        const auto& c=s.tcgs[i];
        if(c.type!=GX_TG_MTX3x4 && c.type!=GX_TG_MTX2x4 && c.type!=GX_TG_SRTG)
            throw std::invalid_argument("GX emboss texgen requires an additional shader path");
        unsigned source=unsigned(c.src);
        if(source>GX_MAX_TEXGENSRC || (source>=GX_TG_TEXCOORD0 && source<=GX_TG_TEXCOORD6))
            throw std::invalid_argument("GX recursive texgen source requires an additional shader path");
        d.type=unsigned(c.type);d.source=source==GX_MAX_TEXGENSRC?GX_TG_TEX0+i:source;
        d.flags=unsigned(c.inputFormAB11);
        if(c.type!=GX_TG_SRTG && c.mtx!=GX_IDENTITY && s.vtxDesc[GX_VA_TEX0MTXIDX+i]==GX_NONE) {
            if(unsigned(c.mtx)>=60)throw std::invalid_argument("GX texture matrix outside shared bank");
            d.matrix=unsigned(c.mtx)/3;
        }
        if(s.dualTex&1) {
            d.flags|=unsigned(c.normalize)<<1;
            if(c.postMtx!=GX_PTIDENTITY) {
                if(unsigned(c.postMtx)<64 || unsigned(c.postMtx)>=124)throw std::invalid_argument("GX post texture matrix outside bank");
                d.post_matrix=(unsigned(c.postMtx)-64)/3;
            }
        }
    }
    return out;
}
}
