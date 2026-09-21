/* SPDX-License-Identifier: GPL-3.0-only */
#include "../gx_copy_color.h"
#include "../gx_depth_offset.h"
typedef unsigned u4 __attribute__((ext_vector_type(4)));
typedef unsigned u8 __attribute__((ext_vector_type(8)));
typedef float f4 __attribute__((ext_vector_type(4)));
typedef __fp16 h2 __attribute__((ext_vector_type(2)));
extern u4 load_uniform(u4,unsigned,unsigned) __asm("llvm.amdgcn.s.buffer.load.v4i32");
extern u8 load_texture(u4,unsigned,unsigned) __asm("llvm.amdgcn.s.buffer.load.v8i32");
extern unsigned load_word(u4,unsigned,unsigned,unsigned) __asm("llvm.amdgcn.raw.buffer.load.i32");
extern f4 sample_bias(unsigned,float,float,float,u8,u4,_Bool,unsigned,unsigned) __asm("llvm.amdgcn.image.sample.b.2d.v4f32.f32.f32");
extern void export_pixel(unsigned,unsigned,h2,h2,_Bool,_Bool) __asm("llvm.amdgcn.exp.compr.v2f16");
static inline float interp(float i,float j,unsigned lane,unsigned mask){
    return __builtin_amdgcn_interp_p2(__builtin_amdgcn_interp_p1(i,lane,0,mask),j,lane,0,mask);
}
static inline MkwCopyColor sample(float x,float y,u8 texture,u4 sampler,f4 clamp){
    y=y<clamp.x?clamp.x:y>clamp.y?clamp.y:y;
    f4 c=sample_bias(15,0,x,y,texture,sampler,0,0,0);return (MkwCopyColor){c.x,c.y,c.z,c.w};
}
/* Depth copy: the D32 target is read through a raw buffer view and tiled by
 * the shared Tiled32_4 equation; no texture descriptor exists for it. */
static inline unsigned depth_z24(int x,int y,u4 dims,u4 db,int top,int bottom){
    y=y<top?top:y>bottom?bottom:y;
    x=x<0?0:x>=(int)dims.x?(int)dims.x-1:x;
    y=y<0?0:y>=(int)dims.y?(int)dims.y-1:y;
    float d=__builtin_bit_cast(float,load_word(db,mkw_depth_pixel_offset((unsigned)x,(unsigned)y,dims.z),0,0));
    return mkw_copy_z24(d,1); /* aurora UseReversedZ */
}
static inline void depth_bytes(int x,int y,u4 dims,u4 db,int top,int bottom,unsigned* out){
    unsigned z=depth_z24(x,y,dims,db,top,bottom);
    out[0]=z>>16;out[1]=(z>>8)&255;out[2]=z&255;
}
void gx_copy(u4 data,u4 unused0,u4 unused1,unsigned mask,float i,float j){
    u4 flags=load_uniform(data,80,0);
    f4 filter=__builtin_bit_cast(f4,load_uniform(data,48,0)),clamp=__builtin_bit_cast(f4,load_uniform(data,64,0));
    float x=interp(i,j,0,mask),y=interp(i,j,1,mask);
    float r,g,b,a;
    if(flags.w){
        u4 db=load_uniform(data,96,0),dims=load_uniform(data,112,0);
        int top=(int)__builtin_floorf(clamp.x*(float)dims.y),bottom=(int)__builtin_floorf(clamp.y*(float)dims.y);
        int cx=(int)__builtin_floorf(x*(float)dims.x),cy=(int)__builtin_floorf(y*(float)dims.y);
        cy=cy<top?top:cy>bottom?bottom:cy;
        unsigned b3[3];depth_bytes(cx,cy,dims,db,top,bottom,b3);
        if(flags.x){
            int rs=(int)(filter.w*(float)dims.y+.5f);if(rs<1)rs=1;
            unsigned p3[3],n3[3];
            depth_bytes(cx,cy-rs,dims,db,top,bottom,p3);depth_bytes(cx,cy+rs,dims,db,top,bottom,n3);
            unsigned fa=(unsigned)filter.x,fb=(unsigned)filter.y,fc=(unsigned)filter.z;
            b3[0]=mkw_copy_depth_filter(p3[0],b3[0],n3[0],fa,fb,fc);
            b3[1]=mkw_copy_depth_filter(p3[1],b3[1],n3[1],fa,fb,fc);
            b3[2]=mkw_copy_depth_filter(p3[2],b3[2],n3[2],fa,fb,fc);
        }
        /* Z16 (0x13): {h,h,h,1} so the IA8 pair samples high-Z as intensity.
         * Z24X8 (0x16): {h,m,l,1} in RGBA8 storage. */
        const float unorm=1.f/255.f;
        if(flags.z==0x13){r=g=b=(float)b3[0]*unorm;a=1;}
        else{r=(float)b3[0]*unorm;g=(float)b3[1]*unorm;b=(float)b3[2]*unorm;a=1;}
        h2 drg={(__fp16)r,(__fp16)g},dba={(__fp16)b,(__fp16)a};
        export_pixel(0,15,drg,dba,1,1);
        return;
    }
    u8 texture=load_texture(data,0,0);u4 sampler=load_uniform(data,32,0);
    MkwCopyColor c=sample(x,y,texture,sampler,clamp);
    if(flags.x){
        MkwCopyColor prev=sample(x,y-filter.w,texture,sampler,clamp),next=sample(x,y+filter.w,texture,sampler,clamp);
        c=mkw_copy_filter(prev,c,next,filter.x,filter.y,filter.z);
    }
    if(flags.y)c.a=1;
    c=mkw_copy_convert(c,flags.z);
    h2 rg={(__fp16)c.r,(__fp16)c.g},ba={(__fp16)c.b,(__fp16)c.a};
    export_pixel(0,15,rg,ba,1,1);
}
