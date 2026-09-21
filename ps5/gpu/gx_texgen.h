/* SPDX-License-Identifier: GPL-3.0-only
 * Matrix/SRTG texgen from Aurora shader.cpp. Emboss is not implemented by
 * this path; the state encoder rejects it rather than generating wrong UVs. */
#ifndef MKW_GX_TEXGEN_H
#define MKW_GX_TEXGEN_H
#include "gx_lighting.h"
typedef struct {
    unsigned type,source,matrix,post_matrix;
    unsigned flags,pad0,pad1,pad2;
} MkwTexgen;
typedef struct {unsigned count,used_mask,pad0,pad1;MkwTexgen generators[8];} MkwTexgenState;
#define MKW_GX_TEXGEN_BYTES 272u
#define MKW_GX_TEXGEN_OFFSET 3376u
/* flags: bit 0 forces source Z=1; bit 1 normalizes before post-transform.
 * Identity matrix indices are UINT32_MAX. type 0=3x4, 1=2x4, 10=SRTG. */
static inline float mkw_texgen_dot(MkwLightVec row,MkwLightVec v){return mkw_light_dot(row,v)+row.w*v.w;}
static inline MkwLightVec mkw_texgen_matrix(MkwLightVec v,MkwLightVec a,MkwLightVec b,MkwLightVec c) {
    return (MkwLightVec){mkw_texgen_dot(a,v),mkw_texgen_dot(b,v),mkw_texgen_dot(c,v),1};
}
static inline MkwLightVec mkw_texgen_apply(MkwTexgen config,MkwLightVec input,
    MkwLightVec a,MkwLightVec b,MkwLightVec c,MkwLightVec post_a,MkwLightVec post_b,MkwLightVec post_c) {
    if(config.flags&1)input.z=1;
    MkwLightVec value=input;
    if(config.type==10)value.z=1;
    else {
        if(config.matrix!=~0u)value=mkw_texgen_matrix(input,a,b,c);
        if(config.type==1)value.z=1;
    }
    if(config.flags&2)value=mkw_light_normalize(value);
    value.w=1;
    if(config.post_matrix!=~0u)value=mkw_texgen_matrix(value,post_a,post_b,post_c);
    if(config.type!=10 && value.z==0) {
        value.x=__builtin_fminf(1,__builtin_fmaxf(-1,value.x*.5f));
        value.y=__builtin_fminf(1,__builtin_fmaxf(-1,value.y*.5f));
    }
    return value;
}
#endif
