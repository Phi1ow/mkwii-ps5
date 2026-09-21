// SPDX-License-Identifier: GPL-3.0-only
#include "gx_blend_state.h"
#include <cstdio>
#include <stdexcept>
static unsigned checks;
static void check(bool v){++checks;if(!v)throw std::runtime_error("GX blend check failed");}
int main(int argc,char** argv){try {
    check(argc==2);FILE* f=std::fopen(argv[1],"rb");check(f);
    aurora::gx::GXRegisterState s{};s.blendMode=GX_BM_BLEND;s.pixelFmt=GX_PF_RGBA6_Z24;
    s.colorUpdate=s.alphaUpdate=true;s.dstAlpha=UINT32_MAX;
    unsigned record[4],count=0;
    while(std::fread(record,sizeof(record),1,f)==1){
        s.blendFacSrc=GXBlendFactor(record[0]);s.blendFacDst=GXBlendFactor(record[1]);
        auto blend=mkw::agc::snapshot_gx_blend(s,record[2]);check(blend.control==record[3]);check(blend.targetMask==15);++count;
    }
    std::fclose(f);check(count==64);
    for(unsigned format=0;format<8;++format)for(unsigned mask=0;mask<4;++mask){
        s.pixelFmt=GXPixelFmt(format);s.colorUpdate=mask&1;s.alphaUpdate=mask&2;
        s.blendFacSrc=GX_BL_DSTALPHA;s.blendFacDst=GX_BL_INVDSTALPHA;s.dstAlpha=128;
        auto b=mkw::agc::snapshot_gx_blend(s,0);
        check(b.targetMask==((mask&1?7u:0u)|(format==1 && (mask&2)?8u:0u)));
        check((b.control&31)==(format==1?6u:1u));check(((b.control>>8)&31)==(format==1?7u:0u));
        check(b.constant[3]==(format==1 && (mask&2)?128.f/255:0.f));
    }
    s.pixelFmt=GX_PF_RGBA6_Z24;s.colorUpdate=s.alphaUpdate=true;s.dstAlpha=UINT32_MAX;
    s.blendMode=GX_BM_NONE;check(mkw::agc::snapshot_gx_blend(s,0).control==0x60010001);
    s.blendMode=GX_BM_SUBTRACT;check(mkw::agc::snapshot_gx_blend(s,0).control==0x61810181);
    s.blendMode=GX_BM_LOGIC;
    for(auto op:{GX_LO_AND,GX_LO_XOR,GX_LO_NAND}){s.blendOp=op;bool caught=false;try{mkw::agc::snapshot_gx_blend(s,0);}catch(const std::invalid_argument&){caught=true;}check(caught);}
    std::printf("PASS GX blend: %u checks, 64 SharpProspero register comparisons, format/mask/constant policies\n",checks);
    return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL after %u: %s\n",checks,e.what());return 1;}}
