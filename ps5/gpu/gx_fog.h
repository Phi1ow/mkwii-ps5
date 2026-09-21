/* SPDX-License-Identifier: GPL-3.0-only
 * GX fog uniform block for the direct-TEV fragment shader, following
 * WiiCompiled aurora-main shader.cpp (fog evaluation) and shader_info.cpp
 * (fog_uniform packing). Values are already converted for TEV byte space:
 * the color is stored x255 so it mixes with the TEV result directly.
 */
#ifndef MKW_GX_FOG_H
#define MKW_GX_FOG_H
typedef struct {
    float color[4];     /* GX fog color x255 (TEV byte space); w unused */
    float abc[4];       /* aRaw, float(bMagnitude), c, float(bShift) */
    /* screenSpaceCenter, rangeWidth in target pixels, viewport halfWidth,
     * viewport centerX - the latter two map exported NDC x to pixel x. */
    float rangeBase[4];
    float zmap[4];      /* PA viewport z scale (zfar-znear) and offset (znear) */
    float rangeK[12];   /* K0..K9; entries 10,11 repeat K9 like the reference */
    unsigned ctl[4];    /* fog type (GXFogType), rangeAdjust, reserved */
} MkwFog;
#define MKW_GX_FOG_BYTES 128u
/* Byte offsets inside GxDirectUniforms (appended after indirectMatrices). */
#define MKW_GX_FOG_OFFSET 2160u
#define MKW_GX_FOG_ABC (MKW_GX_FOG_OFFSET+16u)
#define MKW_GX_FOG_RANGEBASE (MKW_GX_FOG_OFFSET+32u)
#define MKW_GX_FOG_ZMAP (MKW_GX_FOG_OFFSET+48u)
#define MKW_GX_FOG_RANGEK (MKW_GX_FOG_OFFSET+64u)
#define MKW_GX_FOG_CTL (MKW_GX_FOG_OFFSET+112u)
#ifdef __AMDGCN__
/* Freestanding GPU code: map to the LLVM intrinsics, not libcalls. */
extern float mkw_fog_sqrt(float) __asm("llvm.sqrt.f32");
extern float mkw_fog_exp2(float) __asm("llvm.exp2.f32");
#else
static inline float mkw_fog_sqrt(float x){return __builtin_sqrtf(x);}
static inline float mkw_fog_exp2(float x){return __builtin_exp2f(x);}
#endif
#endif
