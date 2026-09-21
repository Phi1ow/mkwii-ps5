// SPDX-License-Identifier: GPL-3.0-only
#include "../gpu/gx_tev_program.h"
#include "gx/register_state.hpp"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <cstring>
#include <limits>
namespace gx=aurora::gx;
static unsigned checks;
static void check(bool v,const char* why) {++checks;if(!v)throw std::runtime_error(why);}
static unsigned rng=0x47585445;
static unsigned random_u32(){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
static unsigned pick(unsigned n){return random_u32()%n;}
static constexpr unsigned ops[]={0,1,8,9,10,11,12,13,14,15};
static aurora::Vec4<float> color(int r,int g,int b,int a){return {r/255.f,g/255.f,b/255.f,a/255.f};}
static gx::GXRegisterState state_base(){
    gx::GXRegisterState s{};s.numTevStages=1;s.numTexGens=1;
    for(unsigned i=0;i<4;++i){s.colorRegs[i]=color(10+i,20+i,30+i,40+i);s.kcolors[i]=color(3+i,5+i,7+i,11+i);}
    return s;
}
template<class F> static void invalid(F f){bool caught=false;try{f();}catch(const std::invalid_argument&){caught=true;}check(caught,"invalid GX input accepted");}
static void encoder_checks(){
    gx::AlphaCompare always{};
    check(!bool(always),"default alpha test should be elided");
    always.op=GX_AOP_XOR;check(bool(always),"ALWAYS XOR ALWAYS must retain alpha test");
    always.op=GX_AOP_XNOR;check(!bool(always),"ALWAYS XNOR ALWAYS may skip alpha test");
    auto s=state_base();auto& t=s.tevStages[0];
    t.colorPass={GX_CC_ZERO,GX_CC_TEXC,GX_CC_KONST,GX_CC_C0};
    t.alphaPass={GX_CA_ZERO,GX_CA_TEXA,GX_CA_KONST,GX_CA_A1};
    t.colorOp={GX_TEV_SUB,GX_TB_SUBHALF,GX_CS_SCALE_4,GX_TEVREG2,false};
    t.alphaOp={GX_TEV_ADD,GX_TB_ADDHALF,GX_CS_DIVIDE_2,GX_TEVREG0,true};
    t.kcSel=GX_TEV_KCSEL_7_8;t.kaSel=GX_TEV_KASEL_K2_G;
    t.texCoordId=GX_TEXCOORD_NULL;t.texMapId=GX_TEXMAP3;t.channelId=GX_COLOR1A1;
    s.tevSwapTable[2]={GX_CH_BLUE,GX_CH_RED,GX_CH_ALPHA,GX_CH_GREEN};t.tevSwapTex=GX_TEV_SWAP2;
    s.alphaCompare={GX_GREATER,127,GX_AOP_XOR,GX_EQUAL,255};
    auto p=mkw::gpu::build_tev_program(s);auto a=p.stages[0];
    check(a.tex_coord==0 && a.tex_map==3 && a.sample_enabled,"texture dependency fallback");
    check(a.color_args==(15u|8u<<4|14u<<8|2u<<12) && a.alpha_args==(7u|4u<<3|6u<<6|2u<<9),"selector layout");
    check(a.color_op==(1u|2u<<4|3u<<10) && a.alpha_op==(3u<<4|1u<<6|2u<<8|1u<<10),"operation layout");
    check(a.texture_swap==(2u|0u<<2|3u<<4|1u<<6),"swap layout");
    check(a.konst.r==223.125f && a.konst.a==7,"fractional/color konst");
    check(p.alpha_flags==(4u|2u<<3|2u<<6|127u<<8|255u<<16),"alpha compare layout");
    for(unsigned sel=0;sel<32;++sel){
        if(sel>=8 && sel<12)continue;
        t.kcSel=GXTevKColorSel(sel);p=mkw::gpu::build_tev_program(s);
        float expected=sel<8?(8-sel)*31.875f:sel<16?float(3+sel-12):
            float(((sel-16)>>2)==0?3+((sel-16)&3):((sel-16)>>2)==1?5+((sel-16)&3):((sel-16)>>2)==2?7+((sel-16)&3):11+((sel-16)&3));
        check(p.stages[0].konst.r==expected,"all color konst selectors");
    }
    s.numTexGens=0;p=mkw::gpu::build_tev_program(s);
    check(!p.stages[0].sample_enabled && p.disabled_texture_value==0,"texture disabled without texgens");
    s.numTexGens=1;t.texMapId=GX_TEXMAP_NULL;p=mkw::gpu::build_tev_program(s);
    check(!p.stages[0].sample_enabled && p.disabled_texture_value==255,"texture disabled with texgens");
    auto snapshot=p;s.kcolors[0]=color(255,255,255,255);
    check(!std::memcmp(&p,&snapshot,sizeof p),"snapshot changed after GX state mutation");
    s.numTevStages=0;invalid([&]{mkw::gpu::build_tev_program(s);});s.numTevStages=17;invalid([&]{mkw::gpu::build_tev_program(s);});
    s=state_base();s.tevStages[0].colorPass.a=GXTevColorArg(16);invalid([&]{mkw::gpu::build_tev_program(s);});
    s=state_base();s.colorRegs[0][0]=std::numeric_limits<float>::quiet_NaN();invalid([&]{mkw::gpu::build_tev_program(s);});
    s=state_base();s.tevStages[0].colorOp.scale=GXTevScale(-1);invalid([&]{mkw::gpu::build_tev_program(s);});
    s=state_base();s.tevStages[0].colorPass.a=GX_CC_KONST;s.tevStages[0].kcSel=GXTevKColorSel(8);invalid([&]{mkw::gpu::build_tev_program(s);});
    s.tevStages[0].colorPass.a=GX_CC_ZERO;check(mkw::gpu::build_tev_program(s).stage_count==1,"unused konst rejected");
}
// Exported host evaluator lets the independent Python oracle compare every
// register after every stage, not just a clamped final pixel.
extern "C" __declspec(dllexport) void tev_step(MkwTevRegisters* r,const MkwTevStage* s,
    const MkwTevValue* tex,const MkwTevValue* ras,MkwTevValue* out){*out=mkw_tev_stage(r,*s,*tex,*ras);}
extern "C" __declspec(dllexport) int tev_alpha(float alpha,unsigned flags){return mkw_tev_alpha_test(alpha,flags);}
extern "C" __declspec(dllexport) void tev_raster(const MkwTevStage* s,const MkwTevValue* a,
    const MkwTevValue* b,const MkwTevValue* i,MkwTevValue* out){*out=mkw_tev_raster(*s,*a,*b,*i);}
#ifndef MKW_TEV_DLL
int main(int argc,char**argv){try{
    encoder_checks();if(argc!=2)throw std::runtime_error("Expected fixture binary output path");
    std::FILE* f=std::fopen(argv[1],"wb");if(!f)throw std::runtime_error("Cannot open fixture output");
    for(unsigned n=0;n<2048;++n){
        auto s=state_base();s.numTevStages=1+n%16;s.numTexGens=n%9;s.numIndStages=n%5;
        for(unsigned reg=0;reg<4;++reg){
            s.colorRegs[reg]=color(int(pick(2048))-1024,int(pick(2048))-1024,int(pick(2048))-1024,int(pick(2048))-1024);
            s.kcolors[reg]=color(pick(256),pick(256),pick(256),pick(256));
            s.tevSwapTable[reg]={GXTevColorChan(pick(4)),GXTevColorChan(pick(4)),GXTevColorChan(pick(4)),GXTevColorChan(pick(4))};
        }
        // Most programs pass alpha so their RGB data is observed on console.
        if(n%4==0)s.alphaCompare={GXCompare(pick(8)),pick(256),GXAlphaOp(pick(4)),GXCompare(pick(8)),pick(256)};
        for(unsigned k=0;k<s.numTevStages;++k){auto& t=s.tevStages[k];
            t.colorPass={GXTevColorArg(pick(16)),GXTevColorArg(pick(16)),GXTevColorArg(pick(16)),GXTevColorArg(pick(16))};
            t.alphaPass={GXTevAlphaArg(pick(8)),GXTevAlphaArg(pick(8)),GXTevAlphaArg(pick(8)),GXTevAlphaArg(pick(8))};
            t.colorOp={GXTevOp(ops[pick(10)]),GXTevBias(pick(3)),GXTevScale(pick(4)),GXTevRegID(pick(4)),bool(pick(2))};
            t.alphaOp={GXTevOp(ops[pick(10)]),GXTevBias(pick(3)),GXTevScale(pick(4)),GXTevRegID(pick(4)),bool(pick(2))};
            t.kcSel=GXTevKColorSel(pick(2)?pick(8):12+pick(20));t.kaSel=GXTevKAlphaSel(pick(2)?pick(8):16+pick(16));
            t.texMapId=GXTexMapID(pick(9));t.texCoordId=GXTexCoordID(pick(10));t.channelId=GXChannelID(pick(9));
            t.tevSwapTex=GXTevSwapSel(pick(4));t.tevSwapRas=GXTevSwapSel(pick(4));
            t.indTexStage=GXIndTexStageID(pick(4));t.indTexFormat=GXIndTexFormat(pick(4));t.indTexAlphaSel=GXIndTexAlphaSel(pick(4));
        }
        auto p=mkw::gpu::build_tev_program(s);std::array<unsigned char,2048> record{};
        std::memcpy(record.data(),&p,sizeof p);
        for(unsigned i=0;i<19;++i){
            // Quarter-byte sample values also exercise comparison precision.
            MkwTevValue v={pick(1021)*0.25f,pick(1021)*0.25f,pick(1021)*0.25f,pick(1021)*0.25f};
            if(i==2)v={float(pick(256)),float(pick(256)),float(pick(256)),0};
            std::memcpy(record.data()+1360+i*16,&v,sizeof v);
        }
        if(std::fwrite(record.data(),1,record.size(),f)!=record.size())throw std::runtime_error("Fixture write failed");
    }
    if(std::fclose(f))throw std::runtime_error("Fixture close failed");
    std::printf("PASS GX TEV snapshot: %u checks; 2048 real-state programs emitted\n",checks);
}catch(const std::exception&e){std::fprintf(stderr,"FAIL %s\n",e.what());return 1;}}
#endif
