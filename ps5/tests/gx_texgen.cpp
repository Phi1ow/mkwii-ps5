// SPDX-License-Identifier: GPL-3.0-only
#include "gx_texgen_state.h"
#include <cstdio>
#include <stdexcept>
static unsigned checks;
static void check(bool v,const char* why){++checks;if(!v)throw std::runtime_error(why);}
template<class F>static void rejects(F f){bool caught=false;try{f();}catch(const std::invalid_argument&){caught=true;}check(caught,"unsupported texgen accepted");}
static void near(MkwLightVec a,MkwLightVec b,const char* why){check(std::abs(a.x-b.x)<1e-6f && std::abs(a.y-b.y)<1e-6f && std::abs(a.z-b.z)<1e-6f,why);}
int main(){try{
    namespace gx=aurora::gx;
    gx::GXRegisterState s{};s.numTexGens=8;
    const GXTexGenSrc sources[]={GX_TG_POS,GX_TG_NRM,GX_TG_BINRM,GX_TG_TANGENT,GX_TG_TEX0,GX_TG_TEX1,GX_TG_TEX2,GX_TG_TEX3,GX_TG_TEX4,GX_TG_TEX5,GX_TG_TEX6,GX_TG_TEX7,GX_TG_COLOR0,GX_TG_COLOR1,GX_MAX_TEXGENSRC};
    for(unsigned coordinate=0;coordinate<8;++coordinate)for(auto source:sources)for(auto type:{GX_TG_MTX3x4,GX_TG_MTX2x4,GX_TG_SRTG})for(unsigned dual=0;dual<2;++dual) {
        auto& c=s.tcgs[coordinate];c={};c.type=type;c.src=source;c.mtx=GXTexMtx(57);c.postMtx=GXPTTexMtx(121);c.normalize=true;c.inputFormAB11=true;s.dualTex=dual;
        auto state=mkw::agc::snapshot_gx_texgen(s,1u<<coordinate);const auto& d=state.generators[coordinate];
        check(d.source==(source==GX_MAX_TEXGENSRC?GX_TG_TEX0+coordinate:unsigned(source)),"texgen source/default slot");
        check(d.type==unsigned(type) && d.matrix==(type==GX_TG_SRTG?~0u:19),"matrix/SRTG type and shared matrix mapping");
        check(d.post_matrix==(dual?19:~0u) && d.flags==(dual?3:1),"dual transform gates normalization and post matrix");
        c.postMtx=GX_PTIDENTITY;c.mtx=GX_IDENTITY;auto identity=mkw::agc::snapshot_gx_texgen(s,1u<<coordinate);
        check(identity.generators[coordinate].matrix==~0u && identity.generators[coordinate].post_matrix==~0u,"identity sentinels");
    }
    s.tcgs[0].type=GX_TG_BUMP0;rejects([&]{mkw::agc::snapshot_gx_texgen(s,1);});mkw::agc::snapshot_gx_texgen(s,128);
    s.tcgs[0].type=GX_TG_MTX3x4;s.tcgs[0].src=GX_TG_TEXCOORD0;rejects([&]{mkw::agc::snapshot_gx_texgen(s,1);});
    s.tcgs[0].src=GX_TG_POS;s.tcgs[0].mtx=GXTexMtx(63);rejects([&]{mkw::agc::snapshot_gx_texgen(s,1);});
    s.vtxDesc[GX_VA_TEX0MTXIDX]=GX_DIRECT;check(mkw::agc::snapshot_gx_texgen(s,1).generators[0].matrix==~0u,"vertex matrix override ignores unused static matrix");
    s.tcgs[0].postMtx=GXPTTexMtx(124);s.dualTex=1;rejects([&]{mkw::agc::snapshot_gx_texgen(s,1);});s.dualTex=0;mkw::agc::snapshot_gx_texgen(s,1);
    s.numTexGens=0;rejects([&]{mkw::agc::snapshot_gx_texgen(s,1);});s.numTexGens=9;rejects([&]{mkw::agc::snapshot_gx_texgen(s,0);});
    MkwLightVec zero{},x={1,0,0,0},y={0,1,0,0},z={0,0,1,0};
    MkwTexgen c{0,0,0,~0u,0,0,0,0};
    near(mkw_texgen_apply(c,{.25f,.75f,.5f,1},{2,0,0,.5f},{0,3,0,-.25f},{0,0,4,1},zero,zero,zero),{1,2,3,1},"3x4 rows and translation preserve Q");
    c.matrix=~0u;c.flags=1;near(mkw_texgen_apply(c,{.25f,.75f,.5f,1},zero,zero,zero,zero,zero,zero),{.25f,.75f,1,1},"AB11 applied before matrix");
    c.flags=2;c.post_matrix=0;near(mkw_texgen_apply(c,{3,4,0,1},zero,zero,zero,{1,0,0,1},{0,1,0,2},{0,0,1,3}),{1.6f,2.8f,3,1},"normalization precedes post translation and resets homogeneous W");
    c.flags=0;c.post_matrix=~0u;near(mkw_texgen_apply(c,{4,-6,0,1},zero,zero,zero,zero,zero,zero),{1,-1,0,1},"Q zero clamp without division");
    c.type=1;c.matrix=0;near(mkw_texgen_apply(c,{.25f,.75f,9,1},x,y,z,zero,zero,zero),{.25f,.75f,1,1},"2x4 overrides generated Q");
    c.type=10;c.post_matrix=0;near(mkw_texgen_apply(c,{4,-6,9,1},x,y,z,x,y,zero),{4,-6,0,1},"SRTG ignores base matrix and skips Q zero clamp");
    std::printf("PASS GX texgen: %u checks; eight slots, source/type/matrix policy, dual transforms, normalization order and Q zero; host only\n",checks);
}catch(const std::exception& e){std::fprintf(stderr,"FAIL texgen: %s\n",e.what());return 1;}}
