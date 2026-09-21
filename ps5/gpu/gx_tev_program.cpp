// SPDX-License-Identifier: GPL-3.0-only
#include "gx_tev_program.h"
#include "gx/register_state.hpp"
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <type_traits>

namespace mkw::gpu {
namespace gx=aurora::gx;
static_assert(sizeof(MkwTevStage)==80 && sizeof(MkwTevProgram)==1360);
static_assert(offsetof(MkwTevProgram,stages)==80 && offsetof(MkwTevStage,konst)==64);
static_assert(std::is_standard_layout_v<MkwTevProgram> && std::is_trivially_copyable_v<MkwTevProgram>);
static_assert(GX_CC_CPREV==0 && GX_CC_ZERO==15 && GX_CA_APREV==0 && GX_CA_ZERO==7);
static_assert(GX_TEVPREV==0 && GX_TEVREG2==3 && GX_CH_RED==0 && GX_CH_ALPHA==3);
static_assert(GX_COLOR_ZERO==6 && GX_ALPHA_BUMP==7 && GX_ALPHA_BUMPN==8);
static_assert(GX_NEVER==0 && GX_ALWAYS==7 && GX_AOP_AND==0 && GX_AOP_XNOR==3);

static void require(bool v) { if(!v) throw std::invalid_argument("Invalid GX TEV state"); }
static unsigned operation(const gx::TevOp& v) {
    unsigned op=static_cast<unsigned>(v.op),bias=static_cast<unsigned>(v.bias);
    require((op<=1 || (op>=8 && op<=15)) && unsigned(v.scale)<=GX_CS_DIVIDE_2 && unsigned(v.outReg)<=GX_TEVREG2);
    // Compare operations ignore the bias encoding (BP uses the compare marker).
    if(op>=8)bias=0;
    require(bias<=GX_TB_SUBHALF);
    unsigned shifted_bias=bias==GX_TB_ADDHALF?2:bias==GX_TB_SUBHALF?0:1;
    return op | (unsigned(v.scale)<<4) | (unsigned(v.clamp)<<6) |
        (shifted_bias<<8) | (unsigned(v.outReg)<<10);
}
static unsigned swizzle(const gx::TevSwap& s) {
    require(unsigned(s.red)<=3 && unsigned(s.green)<=3 && unsigned(s.blue)<=3 && unsigned(s.alpha)<=3);
    return unsigned(s.red)|(unsigned(s.green)<<2)|(unsigned(s.blue)<<4)|(unsigned(s.alpha)<<6);
}
static MkwTevValue byte_color(const aurora::Vec4<float>& v,int lo,int hi) {
    float dst[4]{};
    for(unsigned i=0;i<4;++i) {
        float value=v[i]*255.0f;
        require(std::isfinite(value) && value>=float(lo)-0.001f && value<=float(hi)+0.001f);
        dst[i]=__builtin_roundevenf(value); // Recover the original register byte/S11.
    }
    return {dst[0],dst[1],dst[2],dst[3]};
}
static MkwTevValue konst_color(unsigned sel,const MkwTevValue (&colors)[4],bool alpha) {
    if(sel<8)return mkw_tev_splat((8-sel)*(255.0f/8.0f));
    require(sel>=12 && sel<=31 && (!alpha || sel>=16));
    if(sel<16)return colors[sel-12];
    return mkw_tev_splat(mkw_tev_lane(colors[(sel-16)&3],(sel-16)>>2));
}
MkwTevProgram build_tev_program(const gx::GXRegisterState& state) {
    require(state.numTevStages>=1 && state.numTevStages<=16 && state.numTexGens<=8 && state.numIndStages<=4);
    MkwTevProgram out{};
    out.stage_count=state.numTevStages;
    out.num_texgens=state.numTexGens;
    out.disabled_texture_value=state.numTexGens?255:0;
    const auto& ac=state.alphaCompare;
    require(unsigned(ac.comp0)<=GX_ALWAYS && unsigned(ac.comp1)<=GX_ALWAYS && unsigned(ac.op)<=GX_AOP_XNOR && ac.ref0<=255 && ac.ref1<=255);
    out.alpha_flags=unsigned(ac.comp0)|(unsigned(ac.comp1)<<3)|(unsigned(ac.op)<<6)|(ac.ref0<<8)|(ac.ref1<<16);
    out.initial={byte_color(state.colorRegs[0],-1024,1023),byte_color(state.colorRegs[1],-1024,1023),
        byte_color(state.colorRegs[2],-1024,1023),byte_color(state.colorRegs[3],-1024,1023)};
    MkwTevValue konst[4];
    for(unsigned i=0;i<4;++i)konst[i]=byte_color(state.kcolors[i],0,255);
    gx::ShaderConfig config{};
    config.numTexGens=state.numTexGens;config.tevStageCount=state.numTevStages;
    config.tevStages=state.tevStages;config.numIndStages=state.numIndStages;
    for(unsigned i=0;i<out.stage_count;++i) {
        const auto& s=state.tevStages[i]; auto& dst=out.stages[i];
        unsigned color[]={unsigned(s.colorPass.a),unsigned(s.colorPass.b),unsigned(s.colorPass.c),unsigned(s.colorPass.d)};
        unsigned alpha[]={unsigned(s.alphaPass.a),unsigned(s.alphaPass.b),unsigned(s.alphaPass.c),unsigned(s.alphaPass.d)};
        for(unsigned j=0;j<4;++j) {require(color[j]<=15 && alpha[j]<=7);dst.color_args|=color[j]<<(4*j);dst.alpha_args|=alpha[j]<<(3*j);}
        dst.color_op=operation(s.colorOp);dst.alpha_op=operation(s.alphaOp);
        require(unsigned(s.tevSwapTex)<4 && unsigned(s.tevSwapRas)<4);
        dst.texture_swap=swizzle(state.tevSwapTable[s.tevSwapTex]);
        dst.raster_swap=swizzle(state.tevSwapTable[s.tevSwapRas]);
        auto dep=gx::tev_stage_texture_dependency(config,i);
        dst.tex_map=static_cast<unsigned>(dep.texMapId);dst.tex_coord=static_cast<unsigned>(dep.texCoordId);
        dst.sample_enabled=dep.canSampleTexture && dep.combinerUsesTexture;
        dst.channel=unsigned(s.channelId);require(dst.channel<=8 || dst.channel==GX_COLOR_NULL);
        dst.indirect_stage=unsigned(s.indTexStage)<state.numIndStages?unsigned(s.indTexStage):~0u;
        require(unsigned(s.indTexFormat)<=3 && unsigned(s.indTexAlphaSel)<=3);
        dst.indirect_format=unsigned(s.indTexFormat);dst.indirect_alpha=unsigned(s.indTexAlphaSel);
        dst.indirect_matrix=unsigned(s.indTexMtxId);
        dst.indirect_wrap=unsigned(s.indTexWrapS)|(unsigned(s.indTexWrapT)<<8);
        dst.indirect_flags=unsigned(s.indTexBiasSel)|(unsigned(s.indTexUseOrigLOD)<<8)|(unsigned(s.indTexAddPrev)<<9);
        // The indirect coordinate is persistent: a stage that only feeds the
        // next stage's addPrev still updates the tracked coordinate (Aurora
        // tev_stage_needs_fixed_texcoord_state).
        if(i+1<state.numTevStages && state.tevStages[i+1].indTexAddPrev)dst.indirect_flags|=1024;
        // Invalid unused konst selectors must not reject a stage that never reads them.
        bool usesColor=false,usesAlpha=false;
        for(unsigned j=0;j<4;++j){usesColor|=color[j]==GX_CC_KONST;usesAlpha|=alpha[j]==GX_CA_KONST;}
        if(usesColor)dst.konst=konst_color(unsigned(s.kcSel),konst,false);
        if(usesAlpha)dst.konst.a=konst_color(unsigned(s.kaSel),konst,true).a;
    }
    return out;
}
unsigned tev_program_used_texcoords(const gx::GXRegisterState& state) {
    require(state.numTevStages<=16 && state.numTexGens<=8 && state.numIndStages<=4);
    gx::ShaderConfig config{};
    config.numTexGens=state.numTexGens;config.tevStageCount=state.numTevStages;
    config.tevStages=state.tevStages;config.numIndStages=state.numIndStages;
    unsigned used=0;
    for(unsigned i=0;i<state.numTevStages;++i) {
        const auto dep=gx::tev_stage_texture_dependency(config,i);
        // Aurora marks the stage coordinate both for real samples and when the
        // persistent indirect coordinate machinery reads it (needsFixed).
        if(dep.texCoordId>=0 && (dep.needsFixedTexcoordState ||
            (dep.canSampleTexture && dep.combinerUsesTexture)))
            used|=1u<<dep.texCoordId;
        const auto& stage=state.tevStages[i];
        if(unsigned(stage.indTexStage)<state.numIndStages) {
            // The shader samples the indirect stage map whenever the stage is
            // active; GX falls back to coordinate 0 for NULL/out-of-range ids.
            const int coord=gx::tev_effective_texcoord(config,state.indStages[stage.indTexStage].texCoordId);
            if(coord>=0)used|=1u<<coord;
        }
    }
    return used;
}
}
