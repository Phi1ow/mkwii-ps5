// SPDX-License-Identifier: GPL-3.0-only
// Native D32 readback and occlusion verification. No game data is fabricated.
#include "agc_blit.h"
#include <bit>
#include <cstdio>
#include <cstring>
#include <stdexcept>
extern "C" void mkw_diagnostic_log(const char*);
namespace {
using namespace mkw::agc;
// Retain on every failure, including unresolved GPU work.
std::shared_ptr<GpuColorTarget> color;
std::shared_ptr<GpuDepthTarget> depth;
constexpr uint32_t W=257,H=259,Base=0xff102030,Red=0xffc04020,Green=0xff208040;
bool inside(unsigned x,unsigned y,BlitRect r){return x>=r.x&&y>=r.y&&x-r.x<r.width&&y-r.y<r.height;}
void invalidate(const void* p,size_t size){for(size_t i=0;i<size;i+=64)__builtin_ia32_clflush(static_cast<const char*>(p)+i);__atomic_thread_fence(__ATOMIC_SEQ_CST);}
template<class F>void inspect(const char* phase,F expected){
    invalidate(color->data(),color->layout().byte_size());invalidate(depth->data(),depth->layout().byte_size());
    for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x){
        auto [c,z]=expected(x,y);uint32_t actualColor,actualDepth;
        std::memcpy(&actualColor,static_cast<const char*>(color->data())+color->layout().pixel_offset(x,y,0),4);
        std::memcpy(&actualDepth,static_cast<const char*>(depth->data())+depth->layout().pixel_offset(x,y),4);
        if(actualColor!=c||actualDepth!=std::bit_cast<uint32_t>(z)){
            char line[240];std::snprintf(line,sizeof(line),"[mkw-depth] FAIL %s %u,%u color=%08x expected=%08x depth=%08x expected=%08x\n",phase,x,y,actualColor,c,actualDepth,std::bit_cast<uint32_t>(z));
            mkw_diagnostic_log(line);throw std::runtime_error("Native D32/color readback differs");
        }
    }
    char line[160];std::snprintf(line,sizeof(line),"[mkw-depth] PASS %s: %u exact color and D32 pixels\n",phase,W*H);mkw_diagnostic_log(line);
}
bool passes(unsigned compare,float incoming,float stored){
    switch(compare){case 0:return false;case 1:return incoming<stored;case 2:return incoming==stored;case 3:return incoming<=stored;
        case 4:return incoming>stored;case 5:return incoming!=stored;case 6:return incoming>=stored;case 7:return true;}
    throw std::logic_error("Bad reference comparison");
}
}
void mkw_test_depth_target(mkw::agc::AgcBlit& blit){
    using namespace mkw::agc;
    color=std::make_shared<GpuColorTarget>(W,H);depth=std::make_shared<GpuDepthTarget>(W,H);
    color->clear_after_gpu_idle(Base);
    mkw_diagnostic_log("[mkw-depth] begin D32 257x259, mode24, no HTILE/stencil\n");
    blit.fill_depth_tested(color,depth,{0,0,W,H},0,{.75f,DepthCompare::Always,true},0);
    inspect("GPU depth-only clear",[](unsigned,unsigned){return std::pair{Base,.75f};});
    constexpr BlitRect nearRect{13,17,137,151},farRect{80,90,160,150},clearRect{50,60,101,109};
    blit.fill_depth_tested(color,depth,nearRect,Red,{.25f,DepthCompare::Less,true});
    blit.fill_depth_tested(color,depth,farRect,Green,{.5f,DepthCompare::Less,true});
    auto scene=[&](unsigned x,unsigned y){return inside(x,y,nearRect)?std::pair{Red,.25f}:inside(x,y,farRect)?std::pair{Green,.5f}:std::pair{Base,.75f};};
    inspect("near/far occlusion",scene);
    blit.fill_depth_tested(color,depth,clearRect,0,{.875f,DepthCompare::Always,true},0);
    inspect("partial depth clear preserves color",[&](unsigned x,unsigned y){auto p=scene(x,y);if(inside(x,y,clearRect))p.second=.875f;return p;});
    blit.clear_color(color,{0,0,W,H},Base);
    blit.fill_depth_tested(color,depth,{0,0,W,H},0,{.5f,DepthCompare::Always,true},0);
    for(bool write:{false,true}){
        for(unsigned c=0;c<8;++c)for(unsigned row=0;row<3;++row)
            blit.fill_depth_tested(color,depth,{c*32+1,row*80+1,30,78},Red,{.25f*(row+1),DepthCompare(c),write});
        inspect(write?"all comparisons with depth writes":"all comparisons without depth writes",[&](unsigned x,unsigned y){
            unsigned c=x/32,row=y/80;
            bool active=c<8&&row<3&&inside(x,y,{c*32+1,row*80+1,30,78});
            float incoming=.25f*(row+1);bool pass=active&&passes(c,incoming,.5f);
            return std::pair{pass?Red:Base,pass&&write?incoming:.5f};
        });
    }
    if(color->release_after_gpu_idle()||depth->release_after_gpu_idle())throw std::runtime_error("Native depth release failed");
    color.reset();depth.reset();mkw_diagnostic_log("[mkw-depth] PASS all checks; both idle targets released\n");
}
