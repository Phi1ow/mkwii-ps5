/* SPDX-License-Identifier: GPL-3.0-only
 * Aurora shader.cpp lighting_func, expressed as scalar C for CPU/GPU use.
 * Layout enums: attenuation SPEC=0/SPOT=1/NONE=2; diffuse NONE=0/SIGN=1/CLAMP=2.
 * Inputs are the prepared GX light and model-view position/normal. */
#ifndef MKW_GX_LIGHTING_H
#define MKW_GX_LIGHTING_H
typedef struct {float x,y,z,w;} MkwLightVec;
typedef struct {MkwLightVec pos,dir,color,cos_att,dist_att;} MkwLight;
typedef struct {
    unsigned enabled,material_vertex,ambient_vertex,light_mask;
    unsigned diffuse,attenuation,pad0,pad1;
    MkwLightVec material,ambient;
} MkwLightChannel;
typedef struct {MkwLight lights[8];MkwLightChannel channels[4];} MkwLighting;
#define MKW_GX_LIGHTING_BYTES 896u
#define MKW_GX_LIGHT_CHANNELS 640u
#define MKW_GX_LIGHT_CHANNEL_BYTES 64u
#ifdef __AMDGCN__
extern float mkw_light_sqrt(float) __asm("llvm.sqrt.f32");
#else
static inline float mkw_light_sqrt(float x){return __builtin_sqrtf(x);}
#endif
static inline float mkw_light_dot(MkwLightVec a,MkwLightVec b){return (a.x*b.x+a.y*b.y)+a.z*b.z;}
static inline MkwLightVec mkw_light_scale(MkwLightVec a,float s){MkwLightVec v={a.x*s,a.y*s,a.z*s,0};return v;}
static inline float mkw_light_max(float a,float b){return __builtin_fmaxf(a,b);}
static inline MkwLightVec mkw_light_normalize(MkwLightVec n){return mkw_light_scale(n,1.f/mkw_light_sqrt(mkw_light_dot(n,n)));}
static inline MkwLightVec mkw_transform_normal(MkwLightVec normal,MkwLightVec r0,MkwLightVec r1,MkwLightVec r2) {
    MkwLightVec n={mkw_light_dot(normal,r0),mkw_light_dot(normal,r1),mkw_light_dot(normal,r2),0};
    return mkw_light_dot(n,n)>1e-10f?mkw_light_normalize(n):n;
}
static inline float mkw_light_factor(MkwLight light,MkwLightVec position,MkwLightVec normal,unsigned attenuation,unsigned diffuse) {
    MkwLightVec delta={light.pos.x-position.x,light.pos.y-position.y,light.pos.z-position.z,0};
    float dist2=mkw_light_dot(delta,delta),dist=mkw_light_sqrt(dist2);
    MkwLightVec direction;
    if(attenuation==2)direction=dist>0?mkw_light_scale(delta,1.f/mkw_light_max(dist,1e-20f)):normal;
    else direction=mkw_light_scale(delta,1.f/dist);
    float attn=1;
    if(attenuation==1) {
        float cosine=mkw_light_max(0,mkw_light_dot(direction,light.dir));
        MkwLightVec cos_v={1,cosine,cosine*cosine,0},dist_v={1,dist,dist2,0};
        attn=mkw_light_max(0,mkw_light_dot(light.cos_att,cos_v)/mkw_light_dot(light.dist_att,dist_v));
    } else if(attenuation==0) {
        attn=mkw_light_dot(normal,direction)>=0?mkw_light_max(0,mkw_light_dot(normal,light.dir)):0;
        MkwLightVec powers={1,attn,attn*attn,0};
        float numerator=mkw_light_dot(light.cos_att,powers);
        MkwLightVec coefficients=diffuse?mkw_light_normalize(light.dist_att):light.dist_att;
        float denominator=mkw_light_dot(coefficients,powers);
        attn=denominator!=0?mkw_light_max(0,numerator/denominator):(numerator>0?1:0);
    }
    float dot=mkw_light_dot(direction,normal);
    float diff=diffuse==0?1:diffuse==1?dot:mkw_light_max(0,dot);
    return attn*diff;
}
static inline int mkw_light_byte(float v){return (int)__builtin_roundevenf(v*255.f);}
static inline int mkw_light_contribution(float factor,float color){return (int)__builtin_roundevenf(factor*color*255.f);}
static inline int mkw_light_finish(int material,int accumulated,unsigned enabled) {
    if(!enabled)return material;
    int light=accumulated<0?0:accumulated>255?255:accumulated;
    return (material*(light+(light>>7)))>>8;
}
#endif
