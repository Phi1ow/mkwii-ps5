/* SPDX-License-Identifier: GPL-3.0-only
 * Native raw GX attribute-fetch path. The first diagnostic exports TEX0 and
 * CLR0 through the established two-interpolant interface. Matrices still use
 * the diagnostic constant-buffer layout; full GX matrices/lighting/texgen
 * remain separate work. There is no CPU vertex conversion in this path.
 */
#include "../gx_vertex_format.h"
#ifdef MKW_GX_TRANSFORMS
#include "../gx_transform_format.h"
#endif
#ifdef MKW_GX_LIGHTING
#include "../gx_lighting.h"
#endif
#ifdef MKW_GX_TEXGEN
#include "../gx_texgen.h"
#endif
typedef unsigned u4 __attribute__((ext_vector_type(4)));
typedef float f4 __attribute__((ext_vector_type(4)));
#ifdef MKW_VERTEX_HOST_TEST
#define MKW_INTRINSIC(name)
#else
#define MKW_INTRINSIC(name) __asm(name)
#endif
extern unsigned load_word(u4,unsigned,unsigned,unsigned) MKW_INTRINSIC("llvm.amdgcn.raw.buffer.load.i32");
extern u4 load_meta(u4,unsigned,unsigned) MKW_INTRINSIC("llvm.amdgcn.s.buffer.load.v4i32");
extern f4 load_uniform(u4,unsigned,unsigned) MKW_INTRINSIC("llvm.amdgcn.s.buffer.load.v4f32");
extern f4 load_vector(u4,unsigned,unsigned,unsigned) MKW_INTRINSIC("llvm.amdgcn.raw.buffer.load.v4f32");
extern void export_vertex(unsigned,unsigned,float,float,float,float,_Bool,_Bool) MKW_INTRINSIC("llvm.amdgcn.exp.f32");
static inline unsigned read_uint(u4 data,unsigned at,unsigned width,unsigned little) {
    unsigned word=load_word(data,at&~3u,0,0),shift=(at&3u)*8;
    unsigned value=word>>shift;
    if(shift+width*8>32)value|=load_word(data,(at&~3u)+4,0,0)<<(32-shift);
    if(width==1)return value&255;
    if(width==2){value&=65535;return little?value:((value&255)<<8)|(value>>8);}
    if(width==3){value&=0xffffff;return little?value:((value&255)<<16)|(value&0xff00)|(value>>16);}
    return little?value:__builtin_bswap32(value);
}
static inline float number(u4 data,unsigned at,unsigned type,unsigned frac,unsigned little) {
    unsigned width=type<2?1:type<4?2:4,value=read_uint(data,at,width,little);
    if(type==4)return __builtin_bit_cast(float,value);
    int n=type==1?(int)(value^128)-128:type==3?(int)(value^32768)-32768:(int)value;
    float scale=__builtin_bit_cast(float,(127u-frac)<<23);
    return (float)n*scale;
}
static inline f4 attribute_group(u4 data,u4 header,unsigned vertex,unsigned attr,unsigned group) {
    u4 a=load_meta(data,MKW_GX_VERTEX_HEADER_BYTES+attr*MKW_GX_VERTEX_ATTRIBUTE_BYTES,0);
    unsigned mode=a.x&3,type=(a.x>>2)&7,count=(a.x>>5)&15,frac=(a.x>>11)&31;
    unsigned is_color=attr==11 || attr==12;
    if(!mode)return is_color?(f4){1,1,1,1}:attr==10?(f4){1,0,0,0}:(f4){0,0,0,0};
    unsigned width=type<2?1:type<4?2:4,index_count=(a.x>>9)&3;
    if(attr!=10 || count!=9)group=0;
    unsigned group_offset=group*3*width;
    unsigned at=header.z+vertex*header.y+a.y,little=0;
    if(mode!=1) {
        unsigned index_width=mode==3?2:1;
        if(attr==10 && index_count==3){at+=group*index_width;group_offset=0;}
        unsigned index=read_uint(data,at,index_width,0);
        u4 b=load_meta(data,MKW_GX_VERTEX_HEADER_BYTES+attr*MKW_GX_VERTEX_ATTRIBUTE_BYTES+16,0);
        at=b.x+index*a.z-a.w;little=(a.x>>16)&1;
    }
    at+=group_offset;
    if(is_color) {
        if(type==1 || type==2 || type==5) {
            const float scale=0x1.010102p-8f;
            return (f4){read_uint(data,at,1,1)*scale,read_uint(data,at+1,1,1)*scale,
                read_uint(data,at+2,1,1)*scale,type==5?read_uint(data,at+3,1,1)*scale:1};
        }
        unsigned v=read_uint(data,at,type==4?3:2,little);
        if(type==0)return (f4){((v>>11)&31)*(1.f/31.f),((v>>5)&63)*(1.f/63.f),(v&31)*(1.f/31.f),1};
        if(type==3)return (f4){((v>>12)&15)*(1.f/15.f),((v>>8)&15)*(1.f/15.f),((v>>4)&15)*(1.f/15.f),(v&15)*(1.f/15.f)};
        return (f4){((v>>18)&63)*(1.f/63.f),((v>>12)&63)*(1.f/63.f),((v>>6)&63)*(1.f/63.f),(v&63)*(1.f/63.f)};
    }
    return (f4){number(data,at,type,frac,little),count>1?number(data,at+width,type,frac,little):0,
        count>2?number(data,at+2*width,type,frac,little):0,0};
}
static inline f4 attribute(u4 data,u4 header,unsigned vertex,unsigned attr) {
    return attribute_group(data,header,vertex,attr,0);
}
#ifdef MKW_VERTEX_HOST_TEST
void mkw_test_vertex_attribute(u4 data,unsigned vertex,unsigned attr,unsigned group,float* output) {
    f4 v=attribute_group(data,load_meta(data,0,0),vertex,attr,group);
    for(unsigned i=0;i<4;++i)output[i]=v[i];
}
#endif
static inline float dot4(f4 a,f4 b){return ((a.x*b.x+a.y*b.y)+a.z*b.z)+a.w*b.w;}
#ifdef MKW_GX_LIGHTING
static inline MkwLightVec light_vec(f4 v){return (MkwLightVec){v.x,v.y,v.z,v.w};}
static inline MkwLightVec light_load(u4 data,unsigned offset){return light_vec(load_uniform(data,offset,0));}
static inline f4 lighting_channel(u4 uniforms,unsigned channel,MkwLightVec position,MkwLightVec normal,f4 vertex_color) {
    const unsigned start=MKW_GX_TRANSFORM_BYTES;
    unsigned at=start+MKW_GX_LIGHT_CHANNELS+channel*MKW_GX_LIGHT_CHANNEL_BYTES;
    u4 control=load_meta(uniforms,at,0),functions=load_meta(uniforms,at+16,0);
    f4 material=control.y?vertex_color:load_uniform(uniforms,at+32,0);
    f4 ambient=control.z?vertex_color:load_uniform(uniforms,at+48,0);
    int accum[4]={mkw_light_byte(ambient.x),mkw_light_byte(ambient.y),mkw_light_byte(ambient.z),mkw_light_byte(ambient.w)};
    if(control.x) {
        #pragma clang loop unroll(disable)
        for(unsigned i=0;i<8;++i)if(control.w&(1u<<i)) {
            unsigned p=start+i*80;
            MkwLight light={light_load(uniforms,p),light_load(uniforms,p+16),light_load(uniforms,p+32),light_load(uniforms,p+48),light_load(uniforms,p+64)};
            float factor=mkw_light_factor(light,position,normal,functions.y,functions.x);
            accum[0]+=mkw_light_contribution(factor,light.color.x);accum[1]+=mkw_light_contribution(factor,light.color.y);
            accum[2]+=mkw_light_contribution(factor,light.color.z);accum[3]+=mkw_light_contribution(factor,light.color.w);
        }
    }
    const float unorm=0x1.010102p-8f;
    return (f4){mkw_light_finish(mkw_light_byte(material.x),accum[0],control.x)*unorm,
        mkw_light_finish(mkw_light_byte(material.y),accum[1],control.x)*unorm,
        mkw_light_finish(mkw_light_byte(material.z),accum[2],control.x)*unorm,
        mkw_light_finish(mkw_light_byte(material.w),accum[3],control.x)*unorm};
}
#endif
#ifdef MKW_GX_TEXGEN
static inline __attribute__((always_inline)) f4 generate_texcoord(u4 vertices,u4 uniforms,u4 header,unsigned vertex,unsigned coordinate) {
    unsigned at=MKW_GX_TEXGEN_OFFSET+16+coordinate*32;
    u4 a=load_meta(uniforms,at,0),b=load_meta(uniforms,at+16,0);
    MkwTexgen config={a.x,a.y,a.z,a.w,b.x,0,0,0};
    unsigned src=config.source;
    f4 input;
    if(src<4)input=attribute_group(vertices,header,vertex,src==0?9:10,src>=2?src-1:0);
    else if(src<=11){input=attribute(vertices,header,vertex,13+src-4);input.z=1;}
    else input=attribute(vertices,header,vertex,11+src-19);
    input.w=1;
    if(config.type!=10) {
        u4 override=load_meta(vertices,MKW_GX_VERTEX_HEADER_BYTES+(1+coordinate)*MKW_GX_VERTEX_ATTRIBUTE_BYTES,0);
        if(override.x&3)config.matrix=read_uint(vertices,header.z+vertex*header.y+override.y,1,0)/3;
    }
    MkwLightVec r0={0,0,0,0},r1=r0,r2=r0,p0=r0,p1=r0,p2=r0;
    if(config.matrix<20) {
        at=MKW_GX_TRANSFORM_POSTEX+config.matrix*48;
        r0=light_vec(load_vector(uniforms,at,0,0));r1=light_vec(load_vector(uniforms,at+16,0,0));r2=light_vec(load_vector(uniforms,at+32,0,0));
    }
    if(config.post_matrix<20) {
        at=MKW_GX_TRANSFORM_POSTTEXTURE+config.post_matrix*48;
        p0=light_vec(load_vector(uniforms,at,0,0));p1=light_vec(load_vector(uniforms,at+16,0,0));p2=light_vec(load_vector(uniforms,at+32,0,0));
    }
    MkwLightVec value=mkw_texgen_apply(config,light_vec(input),r0,r1,r2,p0,p1,p2);
    return (f4){value.x,value.y,config.type==0?value.z:1,0};
}
#endif
void gx_vertex(u4 vertices,u4 uniforms,unsigned vertex_id) {
    u4 header=load_meta(vertices,0,0);
    f4 p=attribute(vertices,header,vertex_id,9),uv=attribute(vertices,header,vertex_id,13);
    f4 color=attribute(vertices,header,vertex_id,11);
#ifdef MKW_GX_TRANSFORMS
    u4 matrix_attr=load_meta(vertices,MKW_GX_VERTEX_HEADER_BYTES,0);
    unsigned matrix=load_meta(vertices,16,0).x;
    if(matrix_attr.x&3)matrix=read_uint(vertices,header.z+vertex_id*header.y+matrix_attr.y,1,0)/3;
    matrix=matrix<20?matrix:19;
    unsigned offset=MKW_GX_TRANSFORM_POSTEX+matrix*MKW_GX_TRANSFORM_MATRIX_BYTES;
    p.w=1;
    f4 mv={dot4(load_vector(uniforms,offset,0,0),p),dot4(load_vector(uniforms,offset+16,0,0),p),
        dot4(load_vector(uniforms,offset+32,0,0),p),1};
    f4 pos={dot4(load_uniform(uniforms,0,0),mv),dot4(load_uniform(uniforms,16,0),mv),
        dot4(load_uniform(uniforms,32,0),mv),dot4(load_uniform(uniforms,48,0),mv)};
    f4 vp=load_uniform(uniforms,MKW_GX_TRANSFORM_VIEWPORT,0);
    pos.x+=pos.w*vp.x;pos.y+=pos.w*vp.y;
#ifdef MKW_GX_LIGHTING
    unsigned normal_matrix=matrix<10?matrix:9;
    unsigned normal_at=MKW_GX_TRANSFORM_NORMAL+normal_matrix*MKW_GX_TRANSFORM_MATRIX_BYTES;
    MkwLightVec normal=mkw_transform_normal(light_vec(attribute(vertices,header,vertex_id,10)),
        light_vec(load_vector(uniforms,normal_at,0,0)),light_vec(load_vector(uniforms,normal_at+16,0,0)),light_vec(load_vector(uniforms,normal_at+32,0,0)));
    // With a per-vertex PN index, Aurora's absolute normal bank only has ten
    // slots. Current-matrix-only draws instead compact/clamp to slot nine.
    if(matrix>=10 && (matrix_attr.x&3))normal=(MkwLightVec){0,0,0,0};
    f4 rgb=lighting_channel(uniforms,0,light_vec(mv),normal,color);
    f4 alpha=lighting_channel(uniforms,2,light_vec(mv),normal,color);
    color=(f4){rgb.x,rgb.y,rgb.z,alpha.w};
#ifdef MKW_GX_VARYINGS
    f4 color1=attribute(vertices,header,vertex_id,12);
    rgb=lighting_channel(uniforms,1,light_vec(mv),normal,color1);
    alpha=lighting_channel(uniforms,3,light_vec(mv),normal,color1);
    color1=(f4){rgb.x,rgb.y,rgb.z,alpha.w};
#endif
#endif
#else
    f4 pos=load_uniform(uniforms,0,0)*p.x+load_uniform(uniforms,16,0)*p.y+
        load_uniform(uniforms,32,0)*p.z+load_uniform(uniforms,48,0);
#endif
#if defined(MKW_GX_TEXGEN) && !defined(MKW_GX_VARYINGS)
    uv=generate_texcoord(vertices,uniforms,header,vertex_id,0);
#endif
    export_vertex(12,15,pos.x,pos.y,pos.z,pos.w,1,0);
#ifdef MKW_GX_VARYINGS
    #define EXPORT_COORD(N) do { \
        f4 t={0,0,1,0}; \
        if(load_meta(uniforms,MKW_GX_TEXGEN_OFFSET,0).y&(1u<<(N))) \
            t=generate_texcoord(vertices,uniforms,header,vertex_id,(N)); \
        export_vertex(32+(N),15,t.x,t.y,t.z,0,0,0); \
    } while(0)
    EXPORT_COORD(0);EXPORT_COORD(1);EXPORT_COORD(2);EXPORT_COORD(3);
    EXPORT_COORD(4);EXPORT_COORD(5);EXPORT_COORD(6);EXPORT_COORD(7);
    #undef EXPORT_COORD
    export_vertex(40,15,color.x,color.y,color.z,color.w,0,0);
    export_vertex(41,15,color1.x,color1.y,color1.z,color1.w,0,0);
    // Fog varying: clip-space x/z/w. The fragment recovers screen-linear NDC
    // x and depth-buffer z as interp(clipX)/interp(clipW).
    export_vertex(42,15,pos.x,pos.z,pos.w,0,0,0);
#else
    export_vertex(32,15,uv.x,uv.y,uv.z,0,0,0);
    export_vertex(33,15,color.x,color.y,color.z,color.w,0,0);
#endif
}
