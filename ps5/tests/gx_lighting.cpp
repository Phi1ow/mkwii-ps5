// SPDX-License-Identifier: GPL-3.0-only
#include "gx_lighting_state.h"
#include <cstdio>
#include <limits>
#include <stdexcept>
static unsigned checks;
static void check(bool v,const char* why){++checks;if(!v)throw std::runtime_error(why);}
static void near(float a,float b,const char* why){check(std::abs(a-b)<2e-6f,why);}
int main(){try{
    namespace gx=aurora::gx;
    gx::GXRegisterState s{};
    s.lights[0].dir={0,0,10,17};s.lights[0].distAtt={0,0,0,19};
    s.lights[1].dir={3,4,0,0};
    s.lights[2].dir={std::numeric_limits<float>::infinity(),0,0,0};
    s.lights[3].dir={std::numeric_limits<float>::quiet_NaN(),1,0,0};
    for(unsigned i=0;i<4;++i){auto& c=s.colorChannelConfig[i];c.lightingEnabled=i&1;c.matSrc=i&1?GX_SRC_VTX:GX_SRC_REG;c.ambSrc=i&1?GX_SRC_REG:GX_SRC_VTX;c.diffFn=GXDiffuseFn(i%3);c.attnFn=GXAttnFn(i%3);s.colorChannelState[i].lightMask=1u<<(7-i);s.colorChannelState[i].matColor={.1f,.2f,.3f,.4f};s.colorChannelState[i].ambColor={.5f,.6f,.7f,.8f};}
    auto copy=mkw::agc::snapshot_gx_lighting(s);s.lights={};s.colorChannelState={};
    check(copy.lights[0].dir.z==1 && copy.lights[0].dir.w==17 && copy.lights[0].dist_att.x==1e-5f,"prepared direction and near-zero attenuation guard");
    near(copy.lights[1].dir.x,.6f,"double precision direction preparation X");near(copy.lights[1].dir.y,.8f,"direction preparation Y");
    check(copy.lights[2].dir.x==0 && copy.lights[3].dir.x==0 && copy.lights[3].dir.y==0,"nonfinite direction sanitization follows Aurora");
    for(unsigned i=0;i<4;++i){const auto& c=copy.channels[i];check(c.enabled==(i&1) && c.material_vertex==(i&1) && c.ambient_vertex==!(i&1) && c.light_mask==(1u<<(7-i)),"four independent channel selectors/masks copied");check(c.material.w==.4f && c.ambient.x==.5f,"channel colors survive state mutation");}
    s.colorChannelConfig[0].diffFn=GXDiffuseFn(7);bool caught=false;try{mkw::agc::snapshot_gx_lighting(s);}catch(const std::invalid_argument&){caught=true;}check(caught,"invalid enum rejected");
    for(int material=0;material<256;++material)for(int light=-16;light<272;++light){
        int clamped=std::max(0,std::min(255,light));
        int expected=material*(clamped+(clamped>=128?1:0))/256;
        check(mkw_light_finish(material,light,1)==expected,"GX material byte multiplication and accumulator clamp");
        check(mkw_light_finish(material,light,0)==material,"disabled lighting preserves quantized material");
    }
    check(mkw_light_byte(.5f)==128 && mkw_light_contribution(.5f,1)==128,"ties to even 127.5");
    check(mkw_light_contribution(1,2.5f/255.f)==2 && mkw_light_contribution(1,-2.5f/255.f)==-2,"positive/negative even ties");
    MkwLightVec n={0,0,1,0},origin={0,0,0,0};
    auto transformed=mkw_transform_normal({.5f,0,0,0},{0,2,0,99},{0,0,3,99},{4,0,0,99});
    check(transformed.x==0 && transformed.y==0 && transformed.z==1,"normal transform ignores translation and normalizes");
    auto zero=mkw_transform_normal(origin,{1,0,0,0},{0,1,0,0},{0,0,1,0});check(zero.x==0 && zero.y==0 && zero.z==0,"zero normal remains finite zero");
    auto tiny=mkw_transform_normal({1e-6f,0,0,0},{1,0,0,0},{0,1,0,0},{0,0,1,0});check(tiny.x==1e-6f,"small normal retains Aurora threshold");
    MkwLight light{};light.pos={0,0,2,0};light.dir=n;light.cos_att={0,1,0,0};light.dist_att={1,0,0,0};
    for(unsigned diffuse=0;diffuse<3;++diffuse){near(mkw_light_factor(light,origin,n,2,diffuse),1,"NONE attenuation front light");near(mkw_light_factor(light,origin,n,1,diffuse),1,"SPOT front cosine");near(mkw_light_factor(light,origin,n,0,diffuse),1,"SPEC front light");}
    light.pos={0,0,-2,0};near(mkw_light_factor(light,origin,n,2,1),-1,"signed back light");near(mkw_light_factor(light,origin,n,2,2),0,"clamped back light");near(mkw_light_factor(light,origin,n,0,0),0,"specular back hemisphere guard");
    light.pos=origin;near(mkw_light_factor(light,origin,n,2,1),1,"NONE coincident light uses normal");
    light.pos={0,0,2,0};light.cos_att={1,2,3,0};light.dist_att={1,2,3,0};near(mkw_light_factor(light,origin,n,1,0),6.f/17,"spot cosine/distance polynomials");
    light.cos_att={1,0,0,0};light.dist_att={0,0,0,0};near(mkw_light_factor(light,origin,n,0,0),1,"specular zero denominator positive numerator");light.cos_att={-1,0,0,0};near(mkw_light_factor(light,origin,n,0,0),0,"specular zero denominator nonpositive numerator");
    std::printf("PASS GX lighting: %u checks; prepared snapshots, independent channels, integer rounding/clamp, normal transforms and attenuation; host only\n",checks);
}catch(const std::exception& e){std::fprintf(stderr,"FAIL lighting: %s\n",e.what());return 1;}}
