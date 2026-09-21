/* SPDX-License-Identifier: GPL-3.0-only
 * Color-copy operations from WiiCompiled aurora-main/lib/gfx/tex_copy_conv.cpp.
 * Float RGBA is canonical sampled color; depth-copy formats are separate.
 * Like that source, RGB565 only forces alpha here (no 565 quantization), and
 * RGB5A3 passes through. Do not claim exact Wii EFB precision from this code.
 */
#pragma once
typedef struct {float r,g,b,a;} MkwCopyColor;
static inline float mkw_copy_saturate(float x){return x<0?0:x>1?1:x;}
static inline float mkw_copy_quantize4(float x){return __builtin_floorf(x*16.f)/15.f;}
static inline float mkw_copy_luma(MkwCopyColor c){return c.r*.257f+c.g*.504f+c.b*.098f+16.f/255.f;}
static inline int mkw_copy_color_format(unsigned format){
    return format<=6||format==0x20||format==0x22||format==0x23||(format>=0x27&&format<=0x2c);
}
/* GX_TF_Z16 (0x13) and GX_TF_Z24X8 (0x16): the depth-copy formats whose Wii RAM
 * encodings WiiCompiled's efb_ram_encoder supports (IA8 and RGBA8 storage).
 * GX_TF_Z8 and the GX_CTF_Z* sub-formats remain rejected. */
static inline int mkw_copy_depth_format(unsigned format){
    return format==0x13||format==0x16;
}
/* Depth float to GX 24-bit z, then the {high,mid,low} bytes used by every
 * depth copy format (tex_copy_conv.cpp gx_z24_at_coord/gx_depth_bytes). */
static inline unsigned mkw_copy_z24(float depth,int reversed){
    float d=reversed?1.f-depth:depth;
    d=d<0?0:d>1?1:d;
    unsigned z=(unsigned)(d*16777215.f+.5f);
    return z>0xffffffu?0xffffffu:z;
}
/* Byte-domain vertical copy filter from tex_copy_conv.cpp sample_depth_copy:
 * per-channel prev/current/next bytes, >>6 accumulation, nine-bit wrap when
 * the coefficient sum reaches 128, then saturate. */
static inline unsigned mkw_copy_depth_filter(unsigned prev,unsigned cur,unsigned next,
    unsigned a,unsigned b,unsigned c){
    unsigned filtered=(prev*a+cur*b+next*c)>>6;
    if(a+b+c>=128)filtered&=0x1ff;
    return filtered>255?255:filtered;
}
static inline MkwCopyColor mkw_copy_convert(MkwCopyColor c,unsigned format){
    float intensity=mkw_copy_luma(c),v=0;
    switch(format){
    case 0:v=mkw_copy_quantize4(intensity);c=(MkwCopyColor){v,v,v,v};break;
    case 1:c=(MkwCopyColor){intensity,intensity,intensity,intensity};break;
    case 2:v=mkw_copy_quantize4(intensity);c=(MkwCopyColor){v,v,v,mkw_copy_quantize4(c.a)};break;
    case 3:c=(MkwCopyColor){intensity,intensity,intensity,c.a};break;
    case 4:c.a=1;break;
    case 0x20:v=mkw_copy_quantize4(c.r);c=(MkwCopyColor){v,v,v,v};break;
    case 0x22:v=mkw_copy_quantize4(c.r);c=(MkwCopyColor){v,v,v,mkw_copy_quantize4(c.a)};break;
    case 0x23:c=(MkwCopyColor){c.r,c.r,c.r,c.a};break;
    case 0x27:c=(MkwCopyColor){c.a,c.a,c.a,c.a};break;
    case 0x28:c=(MkwCopyColor){c.r,c.r,c.r,c.r};break;
    case 0x29:c=(MkwCopyColor){c.g,c.g,c.g,c.g};break;
    case 0x2a:c=(MkwCopyColor){c.b,c.b,c.b,c.b};break;
    case 0x2b:c=(MkwCopyColor){c.r,c.r,c.r,c.g};break;
    case 0x2c:c=(MkwCopyColor){c.g,c.g,c.g,c.b};break;
    }
    return (MkwCopyColor){mkw_copy_saturate(c.r),mkw_copy_saturate(c.g),mkw_copy_saturate(c.b),mkw_copy_saturate(c.a)};
}
static inline float mkw_copy_filter_channel(float prev,float current,float next,float a,float b,float c){
    float x=__builtin_floorf(prev*255.f+.5f)*a+__builtin_floorf(current*255.f+.5f)*b+__builtin_floorf(next*255.f+.5f)*c;
    return mkw_copy_saturate(__builtin_floorf(x/64.f)/255.f);
}
static inline MkwCopyColor mkw_copy_filter(MkwCopyColor p,MkwCopyColor c,MkwCopyColor n,float a,float b,float d){
    return (MkwCopyColor){mkw_copy_filter_channel(p.r,c.r,n.r,a,b,d),mkw_copy_filter_channel(p.g,c.g,n.g,a,b,d),
        mkw_copy_filter_channel(p.b,c.b,n.b,a,b,d),c.a};
}
