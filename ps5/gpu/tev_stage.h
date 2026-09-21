/* SPDX-License-Identifier: GPL-3.0-only
 * Aurora TEV input selection and stage evaluation, shared by C/C++ and GPU.
 * Values are in byte space (1.0 color = 255), retaining fractional sampled
 * colors and fractional konst inputs until the operation's own quantization.
 * Only validated GX enums are accepted; gx_tev_program.cpp performs validation.
 */
#ifndef MKW_TEV_STAGE_H
#define MKW_TEV_STAGE_H
#include "tev_integer.h"

typedef struct { float r, g, b, a; } MkwTevValue;
typedef struct { MkwTevValue prev, reg0, reg1, reg2; } MkwTevRegisters;
typedef struct {
    /* Packed selectors A,B,C,D, four bits each; alpha uses three bits each. */
    unsigned color_args, alpha_args;
    /* op[0:3], scale[4:5], clamp[6], bias+1[8:9], destination[10:11]. */
    unsigned color_op, alpha_op;
    /* RGBA swizzles: four two-bit indices. */
    unsigned texture_swap, raster_swap;
    unsigned tex_map, tex_coord;
    unsigned channel, sample_enabled;
    unsigned indirect_stage, indirect_format, indirect_alpha;
    unsigned indirect_matrix, indirect_wrap, indirect_flags;
    MkwTevValue konst; /* RGB from kcSel; alpha from kaSel. */
} MkwTevStage;

typedef struct {
    unsigned stage_count, alpha_flags, disabled_texture_value, num_texgens;
    MkwTevRegisters initial;
    MkwTevStage stages[16];
} MkwTevProgram;

static inline MkwTevValue mkw_tev_splat(float f) {
    MkwTevValue v = {f,f,f,f}; return v;
}
static inline float mkw_tev_lane(MkwTevValue v, unsigned lane) {
    return lane == 0 ? v.r : lane == 1 ? v.g : lane == 2 ? v.b : v.a;
}
static inline MkwTevValue mkw_tev_swizzle(MkwTevValue v, unsigned sw) {
    MkwTevValue out = {mkw_tev_lane(v,sw&3),mkw_tev_lane(v,(sw>>2)&3),
        mkw_tev_lane(v,(sw>>4)&3),mkw_tev_lane(v,(sw>>6)&3)};
    return out;
}
/* indirect has already been sampled/rounded into Aurora's S,T,U domain
 * (texture .a,.b,.g). Sampling and coordinates belong to the texture backend.
 */
static inline MkwTevValue mkw_tev_raster(MkwTevStage s, MkwTevValue raster0,
    MkwTevValue raster1, MkwTevValue indirect) {
    if(s.channel<6)return (s.channel&1)?raster1:raster0;
    if((s.channel==7 || s.channel==8) && s.indirect_stage<4 && s.indirect_alpha) {
        unsigned sample=(unsigned)mkw_tev_lane(indirect,s.indirect_alpha-1);
        unsigned mask=s.indirect_format==1?0xe0u:s.indirect_format==2?0xf0u:0xf8u;
        float value=(float)(sample&mask);
        if(s.channel==8)value*=255.0f/248.0f;
        return mkw_tev_splat(value);
    }
    return mkw_tev_splat(0);
}
static inline MkwTevValue mkw_tev_register(MkwTevRegisters v, unsigned reg) {
    return reg == 0 ? v.prev : reg == 1 ? v.reg0 : reg == 2 ? v.reg1 : v.reg2;
}
static inline MkwTevValue mkw_tev_color_arg(MkwTevRegisters regs, unsigned arg,
    MkwTevValue tex, MkwTevValue ras, MkwTevValue konst) {
    if (arg < 8) {
        MkwTevValue v = mkw_tev_register(regs, arg >> 1);
        return (arg & 1) ? mkw_tev_splat(v.a) : v;
    }
    switch (arg) {
        case 8: return tex; case 9: return mkw_tev_splat(tex.a);
        case 10: return ras; case 11: return mkw_tev_splat(ras.a);
        case 12: return mkw_tev_splat(255); case 13: return mkw_tev_splat(127.5f);
        case 14: return konst; default: return mkw_tev_splat(0);
    }
}
static inline float mkw_tev_alpha_arg(MkwTevRegisters regs, unsigned arg,
    MkwTevValue tex, MkwTevValue ras, MkwTevValue konst) {
    if (arg < 4) return mkw_tev_register(regs,arg).a;
    return arg == 4 ? tex.a : arg == 5 ? ras.a : arg == 6 ? konst.a : 0;
}
static inline float mkw_tev_wrap(float v) {
    return v - __builtin_floorf(v * (1.0f / 256.0f)) * 256.0f;
}
static inline MkwTevValue mkw_tev_wrap4(MkwTevValue v) {
    MkwTevValue out = {mkw_tev_wrap(v.r),mkw_tev_wrap(v.g),mkw_tev_wrap(v.b),mkw_tev_wrap(v.a)};
    return out;
}
static inline int mkw_tev_round(float v) {
    /* WGSL round is nearest, ties to even (not C round's ties away). */
    return (int)__builtin_roundevenf(v);
}
static inline int mkw_tev_compare(MkwTevValue a, MkwTevValue b, unsigned op) {
    float lhs = a.r, rhs = b.r;
    if (op >= 10) { lhs += a.g * 256.0f; rhs += b.g * 256.0f; }
    if (op >= 12) { lhs += a.b * 65536.0f; rhs += b.b * 65536.0f; }
    int x=mkw_tev_round(lhs), y=mkw_tev_round(rhs);
    return (op&1) ? x==y : x>y;
}
static inline float mkw_tev_eval(float a, float b, float c, float d,
    unsigned flags, int packed_compare) {
    unsigned op=flags&15;
    float result;
    if (op < 2) {
        int bias=(int)((flags>>8)&3)*128-128;
        result=(float)mkw_tev_regular(mkw_tev_round(a),mkw_tev_round(b),
            mkw_tev_round(c),mkw_tev_round(d),op,bias,(flags>>4)&3);
    } else {
        int pass=packed_compare;
        if (op>=14) pass=(op&1) ? mkw_tev_round(a)==mkw_tev_round(b) : mkw_tev_round(a)>mkw_tev_round(b);
        result=d+(pass?c:0.0f);
    }
    float lo=(flags&64)?0.0f:-1024.0f, hi=(flags&64)?255.0f:1023.0f;
    return result<lo?lo:result>hi?hi:result;
}
static inline MkwTevValue mkw_tev_merge(MkwTevValue old, MkwTevValue value, int rgb, int alpha) {
    MkwTevValue out={rgb?value.r:old.r,rgb?value.g:old.g,rgb?value.b:old.b,alpha?value.a:old.a};
    return out;
}
static inline void mkw_tev_write(MkwTevRegisters* regs, unsigned rgb_dst,
    unsigned alpha_dst, MkwTevValue value) {
    /* Unconditional writes let LLVM promote each register to SSA rather than
     * selecting a private-memory address for a dynamically selected output.
     */
    regs->prev=mkw_tev_merge(regs->prev,value,rgb_dst==0,alpha_dst==0);
    regs->reg0=mkw_tev_merge(regs->reg0,value,rgb_dst==1,alpha_dst==1);
    regs->reg1=mkw_tev_merge(regs->reg1,value,rgb_dst==2,alpha_dst==2);
    regs->reg2=mkw_tev_merge(regs->reg2,value,rgb_dst==3,alpha_dst==3);
}
static inline MkwTevValue mkw_tev_stage(MkwTevRegisters* regs, MkwTevStage stage,
    MkwTevValue texture, MkwTevValue raster) {
    texture=mkw_tev_swizzle(texture,stage.texture_swap);
    raster=mkw_tev_swizzle(raster,stage.raster_swap);
    unsigned c=stage.color_args, a=stage.alpha_args;
    MkwTevValue ca=mkw_tev_wrap4(mkw_tev_color_arg(*regs,c&15,texture,raster,stage.konst));
    MkwTevValue cb=mkw_tev_wrap4(mkw_tev_color_arg(*regs,(c>>4)&15,texture,raster,stage.konst));
    MkwTevValue cc=mkw_tev_wrap4(mkw_tev_color_arg(*regs,(c>>8)&15,texture,raster,stage.konst));
    MkwTevValue cd=mkw_tev_color_arg(*regs,(c>>12)&15,texture,raster,stage.konst);
    float aa=mkw_tev_wrap(mkw_tev_alpha_arg(*regs,a&7,texture,raster,stage.konst));
    float ab=mkw_tev_wrap(mkw_tev_alpha_arg(*regs,(a>>3)&7,texture,raster,stage.konst));
    float ac=mkw_tev_wrap(mkw_tev_alpha_arg(*regs,(a>>6)&7,texture,raster,stage.konst));
    float ad=mkw_tev_alpha_arg(*regs,(a>>9)&7,texture,raster,stage.konst);
    int color_compare=mkw_tev_compare(ca,cb,stage.color_op&15);
    int alpha_compare=mkw_tev_compare(ca,cb,stage.alpha_op&15);
    MkwTevValue result={
        mkw_tev_eval(ca.r,cb.r,cc.r,cd.r,stage.color_op,color_compare),
        mkw_tev_eval(ca.g,cb.g,cc.g,cd.g,stage.color_op,color_compare),
        mkw_tev_eval(ca.b,cb.b,cc.b,cd.b,stage.color_op,color_compare),
        mkw_tev_eval(aa,ab,ac,ad,stage.alpha_op,alpha_compare)};
    mkw_tev_write(regs,(stage.color_op>>10)&3,(stage.alpha_op>>10)&3,result);
    return result; /* Last stage selects these destinations for final output. */
}
static inline int mkw_tev_alpha_compare(unsigned value,unsigned ref,unsigned op) {
    switch(op) {
        case 0:return 0; case 1:return value<ref; case 2:return value==ref;
        case 3:return value<=ref; case 4:return value>ref; case 5:return value!=ref;
        case 6:return value>=ref; default:return 1;
    }
}
static inline int mkw_tev_alpha_test(float alpha,unsigned flags) {
    float wrapped=mkw_tev_wrap(alpha);
    unsigned a=(unsigned)mkw_tev_round(wrapped>255.0f?255.0f:wrapped);
    int first=mkw_tev_alpha_compare(a,(flags>>8)&255,flags&7);
    int second=mkw_tev_alpha_compare(a,(flags>>16)&255,(flags>>3)&7);
    unsigned op=(flags>>6)&3;
    return op==0?(first&&second):op==1?(first||second):op==2?(first!=second):(first==second);
}
#endif
