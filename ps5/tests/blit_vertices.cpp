// SPDX-License-Identifier: GPL-3.0-only
#include "agc_blit.h"
#include <cmath>
#include <cstdio>
#include <stdexcept>
using namespace mkw::agc;
static unsigned checks;
static void check(bool ok){++checks;if(!ok)throw std::runtime_error("Blit geometry mismatch");}
static bool near(float a,float b){return std::abs(a-b)<0.000001f;}
int main(){try{
    auto v=blit_vertices(1920,1080,960,540,{240,135,720,810},{0,0,960,540});
    check(v[0].position[0]==-1&&v[0].position[1]==1);
    check(v[2].position[0]==1&&v[2].position[1]==-1);
    check(v[0].normal[0]==.125f&&v[0].normal[1]==.125f);
    check(v[2].normal[0]==.5f&&v[2].normal[1]==.875f);
    auto inset=blit_vertices(64,32,80,40,{16,8,32,16},{20,10,40,20});
    check(inset[0].position[0]==-.5f&&inset[0].position[1]==.5f);
    check(inset[2].position[0]==.5f&&inset[2].position[1]==-.5f);
    check(inset[0].normal[0]==.25f&&inset[2].normal[1]==.75f);
    // Independent screen-space interpolation: each destination pixel center
    // must map to the corresponding source sample center, including odd sizes.
    for(uint32_t width:{1u,3u,17u,639u})for(uint32_t height:{1u,5u,359u}){
        auto quad=blit_vertices(width,height,width,height,{0,0,width,height},{0,0,width,height});
        for(auto p:quad)check(p.color==0xffffffff&&p.position[2]==.5f&&p.normal[2]==0);
        for(uint32_t x=0;x<width;++x){float t=(x+.5f)/width;
            check(near(quad[0].normal[0]+t*(quad[1].normal[0]-quad[0].normal[0]),(x+.5f)/width));}
    }
    for(BlitRect r: {BlitRect{0,0,0,1},{0,0,1,0},{64,0,1,1},{0,32,1,1},{63,0,2,1},{0,31,1,2},{1,0,UINT32_MAX,1},{UINT32_MAX,0,1,1}}){
        bool rejected=false;try{(void)blit_vertices(64,32,64,32,r,{0,0,64,32});}catch(const std::invalid_argument&){rejected=true;}check(rejected);
        rejected=false;try{(void)blit_vertices(64,32,64,32,{0,0,64,32},r);}catch(const std::invalid_argument&){rejected=true;}check(rejected);
    }
    std::printf("PASS %u blit geometry checks\n",checks);return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL %s\n",e.what());return 1;}}
