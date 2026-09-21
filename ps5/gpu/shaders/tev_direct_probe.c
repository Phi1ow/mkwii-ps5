/* SPDX-License-Identifier: GPL-3.0-only
 * Direct GX texture/TEV shader. GxDirectUniforms is uniform for the whole draw.
 * MKW_GX_VARYINGS selects eight STQ inputs and two raster colors; the older
 * diagnostic interface has one coordinate/raster. MKW_GX_FRAGMENT_OUTPUT
 * enables real alpha discard and RGBA output. Indirect texturing follows the
 * reference WebGPU shader: the indirect stage sample (S,T,U bytes) is shifted
 * by format, biased, multiplied by the S10 matrix and added to the wrapped
 * S17.7 base coordinate, optionally accumulating the previous stage.
 */
#include "../tev_stage.h"
#include "../gx_fog.h"
typedef unsigned u4 __attribute__((ext_vector_type(4)));
typedef unsigned u8 __attribute__((ext_vector_type(8)));
typedef float f4 __attribute__((ext_vector_type(4)));
typedef __fp16 h2 __attribute__((ext_vector_type(2)));
extern u4 load_uniform(u4,unsigned,unsigned) __asm("llvm.amdgcn.s.buffer.load.v4i32");
extern u8 load_texture(u4,unsigned,unsigned) __asm("llvm.amdgcn.s.buffer.load.v8i32");
extern f4 sample_bias(unsigned,float,float,float,u8,u4,_Bool,unsigned,unsigned)
    __asm("llvm.amdgcn.image.sample.b.2d.v4f32.f32.f32");
extern void export_pixel(unsigned,unsigned,h2,h2,_Bool,_Bool) __asm("llvm.amdgcn.exp.compr.v2f16");
#ifdef MKW_GX_FRAGMENT_OUTPUT
extern void discard_if_false(_Bool) __asm("llvm.amdgcn.kill");
extern void export_pixel32(unsigned,unsigned,float,float,float,float,_Bool,_Bool) __asm("llvm.amdgcn.exp.f32");
typedef unsigned short un2 __attribute__((ext_vector_type(2)));
extern void export_unorm16(unsigned,unsigned,un2,un2,_Bool,_Bool) __asm("llvm.amdgcn.exp.compr.v2i16");
#endif
static inline f4 load_float(u4 data,unsigned at) {return __builtin_bit_cast(f4,load_uniform(data,at,0));}
#ifdef MKW_GX_UNORM16_OUTPUT
static inline unsigned short unorm16_component(float v) {
    v=v<0?0:v>255?255:v;
    return (unsigned short)(v*257.f+.5f);
}
#endif
static inline MkwTevValue load_value(u4 data,unsigned at) {
    f4 v=load_float(data,at);MkwTevValue out={v.x,v.y,v.z,v.w};return out;
}
static inline int s24_wrap(int v) {return __builtin_bit_cast(int,(unsigned)v<<8)>>8;}
static inline int indirect_wrap(int v,unsigned wrap) {
    /* GX_ITW_OFF=0, 256..16 = 1..5, GX_ITW_0=6: masks in the S17.7 domain. */
    if(wrap==0)return v;if(wrap>=6)return 0;
    return v&(((256>>(wrap-1))<<7)-1);
}
static inline int indirect_shift(int v,int shift) {return shift>=0?v>>shift:v<<(-shift);}
static inline float interp(float i,float j,unsigned lane,unsigned attr,unsigned mask) {
    float v=__builtin_amdgcn_interp_p1(i,lane,attr,mask);
    return __builtin_amdgcn_interp_p2(v,j,lane,attr,mask);
}
#ifdef MKW_GX_VARYINGS
static inline f4 texcoord(float i,float j,unsigned coordinate,unsigned mask) {
    // LLVM interpolation operands must name a constant attribute. The GX
    // coordinate selector is uniform for a TEV stage, so select one block.
    #define COORD(N) case N:return (f4){interp(i,j,0,N,mask),interp(i,j,1,N,mask),interp(i,j,2,N,mask),0}
    switch(coordinate){COORD(0);COORD(1);COORD(2);COORD(3);COORD(4);COORD(5);COORD(6);COORD(7);}
    #undef COORD
    return (f4){0,0,1,0};
}
#endif
void tev_probe(u4 data,u4 reserved0,u4 reserved1,unsigned mask,float i,float j) {
#ifndef MKW_GX_VARYINGS
    float x=interp(i,j,0,0,mask),y=interp(i,j,1,0,mask);
#ifdef MKW_GX_PROJECTED
    // Aurora projects after perspective-correct interpolation. Q=0 preserves
    // the vertex shader's special clamped XY rather than dividing by zero.
    float q=interp(i,j,2,0,mask);
    if(q!=0){x=x/q;y=y/q;}
#endif
    MkwTevValue raster={interp(i,j,0,1,mask)*255.0f,interp(i,j,1,1,mask)*255.0f,
        interp(i,j,2,1,mask)*255.0f,interp(i,j,3,1,mask)*255.0f};
    MkwTevValue raster1=mkw_tev_splat(0);
#else
    MkwTevValue raster={interp(i,j,0,8,mask)*255.f,interp(i,j,1,8,mask)*255.f,
        interp(i,j,2,8,mask)*255.f,interp(i,j,3,8,mask)*255.f};
    MkwTevValue raster1={interp(i,j,0,9,mask)*255.f,interp(i,j,1,9,mask)*255.f,
        interp(i,j,2,9,mask)*255.f,interp(i,j,3,9,mask)*255.f};
#endif
    u4 header=load_uniform(data,0,0);
    MkwTevRegisters regs={load_value(data,16),load_value(data,32),load_value(data,48),load_value(data,64)};
    MkwTevValue result=regs.prev;
    unsigned count=header.x<16?header.x:16;
    int prevS=0,prevT=0; /* Reference t_TexCoord state for GX_ITM add-previous. */
    #pragma clang loop unroll(disable)
    for(unsigned n=0;n<count;++n) {
        unsigned at=80+n*80;
        u4 a=load_uniform(data,at,0),b=load_uniform(data,at+16,0);
        u4 c=load_uniform(data,at+32,0),d=load_uniform(data,at+48,0);
        MkwTevStage s={a.x,a.y,a.z,a.w,b.x,b.y,b.z,b.w,c.x,c.y,c.z,c.w,
            d.x,d.y,d.z,d.w,load_value(data,at+64)};
        MkwTevValue tex=mkw_tev_splat((float)header.z);
        MkwTevValue indirect=mkw_tev_splat(0);
        int cs=0,ct=0,cu=0;
#ifdef MKW_GX_VARYINGS
        if(s.indirect_stage<4) {
            // Indirect stage sample: base coordinate scaled down, .abg bytes as S,T,U.
            u4 ind=load_uniform(data,2000+s.indirect_stage*16,0);
            f4 ic=texcoord(i,j,ind.y,mask);float ix=ic.x,iy=ic.y;
            if(ic.z!=0){ix=ix/ic.z;iy=iy/ic.z;}
            f4 iscale=load_float(data,1872+ind.y*16);
            int iu=s24_wrap((int)(ix*iscale.x*128.0f))>>ind.z,iv=s24_wrap((int)(iy*iscale.y*128.0f))>>ind.w;
            unsigned ires=1360+ind.x*64;
            u8 itexture=load_texture(data,ires,0);u4 isampler=load_uniform(data,ires+32,0);f4 isize=load_float(data,ires+48);
            f4 isample=sample_bias(15,isize.z,(float)iu*isize.x,(float)iv*isize.y,itexture,isampler,0,0,0);
            cs=mkw_tev_round(isample.w*255.0f);ct=mkw_tev_round(isample.z*255.0f);cu=mkw_tev_round(isample.y*255.0f);
            indirect=(MkwTevValue){(float)cs,(float)ct,(float)cu,0};
        }
#endif
#ifdef MKW_GX_VARYINGS
        /* Aurora's t_TexCoord is persistent state updated by coordinate
         * operations (matrix/wrap/addPrev) or by feeding the next stage's
         * addPrev - even on stages that do not sample a texture. */
        unsigned coordState=(s.indirect_matrix!=0)||(s.indirect_wrap&255)||((s.indirect_wrap>>8)&255)
            ||(s.indirect_flags&512)||(s.indirect_flags&1024);
        int u=0,v=0;
        if((s.sample_enabled||coordState)&&s.tex_coord<8) {
            f4 coord=texcoord(i,j,s.tex_coord,mask);
            float cx=coord.x,cy=coord.y;
            if(coord.z!=0){cx=cx/coord.z;cy=cy/coord.z;}
            f4 scale=load_float(data,1872+s.tex_coord*16);
            /* Aurora truncates before signed 24-bit wrapping. */
            u=s24_wrap((int)(cx*scale.x*128.0f));v=s24_wrap((int)(cy*scale.y*128.0f));
        }
        if(coordState) {
            int offS=0,offT=0;
            if(s.indirect_stage<4 && s.indirect_matrix!=0) {
                unsigned fmtShift=s.indirect_format==1?3:s.indirect_format==2?4:s.indirect_format==3?5:0;
                int is=cs>>fmtShift,it=ct>>fmtShift,iu2=cu>>fmtShift;
                unsigned biasSel=s.indirect_flags&255;int bias=s.indirect_format==0?-128:1;
                if(biasSel&1)is+=bias;if(biasSel&2)it+=bias;if(biasSel&4)iu2+=bias;
                unsigned m=s.indirect_matrix;
                if(m<=3) {
                    // Static 2x3 matrix: S10 mantissas, hardware >>3 then the exponent shift.
                    u4 c0=load_uniform(data,2064+(m-1)*32,0),c1=load_uniform(data,2064+(m-1)*32+16,0);
                    int shift=(int)c1.z;
                    offS=indirect_shift(((int)c0.x*is+(int)c0.z*it+(int)c1.x*iu2)>>3,shift);
                    offT=indirect_shift(((int)c0.y*is+(int)c0.w*it+(int)c1.y*iu2)>>3,shift);
                } else {
                    // Dynamic S/T: base coordinate times the indirect S or T byte, >>8, then shift.
                    unsigned mi=m<=7?m-5:m-9;int factor=m<=7?is:it;
                    u4 c1=load_uniform(data,2064+mi*32+16,0);int shift=(int)c1.z;
                    offS=indirect_shift((u*factor)>>8,shift);offT=indirect_shift((v*factor)>>8,shift);
                }
            }
            int fs=indirect_wrap(u,s.indirect_wrap&255)+offS,ft=indirect_wrap(v,(s.indirect_wrap>>8)&255)+offT;
            if(s.indirect_flags&512){fs+=prevS;ft+=prevT;}
            u=s24_wrap(fs);v=s24_wrap(ft);prevS=u;prevT=v;
        }
#endif
        if(s.sample_enabled) {
            unsigned resource=1360+s.tex_map*64;
            u8 texture=load_texture(data,resource,0);
            u4 sampler=load_uniform(data,resource+32,0);
            f4 size=load_float(data,resource+48);
#ifndef MKW_GX_VARYINGS
            f4 scale=load_float(data,1872+s.tex_coord*16);
            /* Aurora truncates before signed 24-bit wrapping. The intermediate
             * unsigned shift avoids C's undefined negative signed left shift. */
            int u=(int)(x*scale.x*128.0f),v=(int)(y*scale.y*128.0f);
            u=s24_wrap(u);v=s24_wrap(v);
#endif
            f4 sampled=sample_bias(15,size.z,(float)u*size.x,(float)v*size.y,texture,sampler,0,0,0);
            tex=(MkwTevValue){sampled.x*255.0f,sampled.y*255.0f,sampled.z*255.0f,sampled.w*255.0f};
        }
        MkwTevValue ras=mkw_tev_raster(s,raster,raster1,indirect);
        result=mkw_tev_stage(&regs,s,tex,ras);
    }
#ifdef MKW_GX_VARYINGS
    /* GX fog (aurora shader.cpp): the varying carries clip x/z/w; dividing the
     * interpolated x and z by interpolated w recovers screen-linear NDC x and
     * the post-flip depth value the rasterizer wrote. Fog color is stored x255
     * so the mix happens in TEV byte space. */
    u4 fctl=load_uniform(data,MKW_GX_FOG_CTL,0);
    if(fctl.x) {
        float fw=interp(i,j,2,10,mask);
        float fndcX=interp(i,j,0,10,mask)/fw,fndcZ=interp(i,j,1,10,mask)/fw;
        f4 zmap=load_float(data,MKW_GX_FOG_ZMAP);
        float fogDepth=1.f-(fndcZ*zmap.x+zmap.y); /* aurora UseReversedZ */
        fogDepth=fogDepth<0?0:fogDepth>1?1:fogDepth;
        unsigned zcoord=(unsigned)mkw_tev_round(fogDepth*16777216.f);
        f4 abc=load_float(data,MKW_GX_FOG_ABC);
        float fogZe;
        if((fctl.x&8)==0) {
            unsigned shifted=zcoord>>(unsigned)abc.w;
            fogZe=(abc.x*16777216.f)/(abc.y-(float)shifted);
        } else fogZe=abc.x*(float)zcoord/16777216.f;
        if(fctl.y) {
            f4 base=load_float(data,MKW_GX_FOG_RANGEBASE);
            float rangeOffset=2.f*((fndcX*base.z+base.w)/base.y)-1.f-base.x;
            float fi=9.f-__builtin_fabsf(rangeOffset)*9.f;
            fi=fi<0?0:fi>9?9:fi;
            unsigned lo=(unsigned)fi;
            f4 klo4=load_float(data,MKW_GX_FOG_RANGEK+(lo>>2)*16);
            f4 khi4=load_float(data,MKW_GX_FOG_RANGEK+((lo+1)>>2)*16);
            unsigned li=lo&3u,hi=(lo+1)&3u;
            float klo=li==0?klo4.x:li==1?klo4.y:li==2?klo4.z:klo4.w;
            float khi=hi==0?khi4.x:hi==1?khi4.y:hi==2?khi4.z:khi4.w;
            float k=klo+(khi-klo)*(fi-(float)lo);
            fogZe*=mkw_fog_sqrt(rangeOffset*rangeOffset+k*k)/k;
        }
        float fogF=fogZe-abc.z;fogF=fogF<0?0:fogF>1?1:fogF;
        float fogZ;
        switch(fctl.x) {
            case 4:case 12: fogZ=1.f-mkw_fog_exp2(-8.f*fogF);break;
            case 5:case 13: fogZ=1.f-mkw_fog_exp2(-8.f*fogF*fogF);break;
            case 6:case 14: fogZ=mkw_fog_exp2(-8.f*(1.f-fogF));break;
            case 7:case 15: {float q=1.f-fogF;fogZ=mkw_fog_exp2(-8.f*q*q);break;}
            default: fogZ=fogF;break;
        }
        fogZ=fogZ<0?0:fogZ>1?1:fogZ;
        f4 fcol=load_float(data,MKW_GX_FOG_OFFSET);
        result.r+=(fcol.x-result.r)*fogZ;
        result.g+=(fcol.y-result.g)*fogZ;
        result.b+=(fcol.z-result.b)*fogZ;
    }
#endif
    // The alpha helper performs the GX overflow before its comparison.
    // Legacy probes visualize the decision; the fragment variant discards.
    int pass=mkw_tev_alpha_test(result.a,header.y);result=mkw_tev_wrap4(result);
    const float unorm=0x1.010102p-8f;
#ifdef MKW_GX_FRAGMENT_OUTPUT
    // LLVM kill(false) removes the pixel from the live mask. This is a real
    // discard: framebuffer color/alpha survive where the GX test fails.
    discard_if_false(pass!=0);
#ifdef MKW_GX_UNORM16_OUTPUT
    // 65535=255*257: every GX byte is represented exactly by UNORM16.
    // Round fractional values to the nearest 16-bit normalized component.
    un2 rg={unorm16_component(result.r),unorm16_component(result.g)};
    un2 ba={unorm16_component(result.b),unorm16_component(result.a)};
    export_unorm16(0,15,rg,ba,1,1);
#elif defined(MKW_GX_FP32_OUTPUT)
    export_pixel32(0,15,result.r*unorm,result.g*unorm,result.b*unorm,result.a*unorm,1,1);
#else
    // Ordinary float-to-half conversion rounds to nearest, ties to even.
    // The previous explicit RTZ intrinsic biased normalized color downward.
    h2 rg={( __fp16)(result.r*unorm),(__fp16)(result.g*unorm)};
    h2 ba={( __fp16)(result.b*unorm),(__fp16)(result.a*unorm)};
    export_pixel(0,15,rg,ba,1,1);
#endif
#else
    export_pixel(0,15,__builtin_amdgcn_cvt_pkrtz(pass?result.r*unorm:0,pass?result.g*unorm:0),
        __builtin_amdgcn_cvt_pkrtz(pass?result.b*unorm:0,pass?1.0f:0),1,1);
#endif
}
