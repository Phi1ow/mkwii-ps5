// SPDX-License-Identifier: GPL-3.0-only
#include "gx_copy_readback.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <stdexcept>
using namespace mkw::agc;
static unsigned checks;
static void check(bool b){++checks;if(!b)throw std::runtime_error("GX RAM readback mismatch");}
template<class F> static void rejects(F f){bool failed=false;try{f();}catch(const std::exception&){failed=true;}check(failed);}
void test_gx_copy_readbacks(){
    // Independent Wii tile byte patterns for already-converted colors. Odd
    // image dimensions exercise the encoder's edge padding in each layout.
    struct Case{unsigned format,color,first,second;bool pairs;};
    const Case cases[]={
        {0,0x77777777,0x77,0,false},{1,0x74747474,0x74,0,false},{2,0x55777777,0x57,0,false},
        {3,0x50747474,0x50,0x74,true},{4,0xff4080c0,0x44,0x18,true},{5,0x504080c0,0x24,0x8c,true},
        {0x20,0x44444444,0x44,0,false},{0x22,0x55444444,0x54,0,false},{0x23,0x50404040,0x50,0x40,true},
        {0x27,0x50505050,0x50,0,false},{0x28,0x40404040,0x40,0,false},{0x29,0x80808080,0x80,0,false},
        {0x2a,0xc0c0c0c0,0xc0,0,false},{0x2b,0x80404040,0x80,0x40,true},{0x2c,0xc0808080,0xc0,0x80,true}
    };
    auto target=std::make_shared<GpuColorTarget>(5,3);
    for(auto t:cases){target->clear_after_gpu_idle(t.color);GxColorCopy copy{5,3,target,GXTexFmt(t.format)};
        auto bytes=encode_native_color_copy(copy);
        check(bytes.size()==(t.pairs?64u:32u));
        for(size_t i=0;i<bytes.size();++i)check(bytes[i]==(t.pairs&&(i&1)?t.second:t.first));
    }
    // RGBA8 stores two planes per 4x4 tile: AR, then GB. A nonuniform
    // source checks AGC detiling, channel order, Wii tiling and border padding.
    target->clear_after_gpu_idle(0xdeadcafe);
    for(unsigned y=0;y<3;++y)for(unsigned x=0;x<5;++x){uint32_t value=0xa0000033|((x+1)<<16)|((y+1)<<8);
        std::memcpy(static_cast<char*>(target->data())+target->layout().pixel_offset(x,y,0),&value,4);}
    GxColorCopy copy{5,3,target,GX_TF_RGBA8};auto rgba=encode_native_color_copy(copy);check(rgba.size()==128);
    for(unsigned tile=0;tile<2;++tile)for(unsigned y=0;y<4;++y)for(unsigned x=0;x<4;++x){
        unsigned i=y*4+x,gx=std::min(tile*4+x,4u),gy=std::min(y,2u);
        check(rgba[tile*64+i*2]==0xa0);check(rgba[tile*64+i*2+1]==gx+1);
        check(rgba[tile*64+32+i*2]==gy+1);check(rgba[tile*64+32+i*2+1]==0x33);
    }
    rejects([&]{(void)encode_native_color_copy({4,3,target,GX_TF_RGBA8});});
    // Depth copies share the IA8 (Z16) and RGBA8 (Z24X8) Wii RAM layouts; the
    // shader-produced bytes are already final, so encoding is a pure detile.
    target->clear_after_gpu_idle(0xa1b2c3d4); // BGRA: b=d4 g=c3 r=b2 a=a1
    auto z16=encode_native_color_copy({5,3,target,GX_TF_Z16});
    check(z16.size()==64);
    for(size_t i=0;i<z16.size();++i)check(z16[i]==(i&1?0xb2:0xa1));
    auto z24=encode_native_color_copy({5,3,target,GX_TF_Z24X8});
    check(z24.size()==128);
    for(size_t i=0;i<z24.size();++i)check(z24[i]==((i&32)?(i&1?0xd4:0xc3):(i&1?0xb2:0xa1)));
    rejects([&]{(void)encode_native_color_copy({5,3,target,GX_TF_Z8});});
    rejects([&]{(void)encode_native_color_copy({5,3,{},GX_TF_RGBA8});});
    check(target->release_after_gpu_idle()==0);rejects([&]{(void)encode_native_color_copy(copy);});
    std::printf("PASS %u GX copy RAM checks: 16 color formats plus Z16/Z24X8 depth layouts, BGRA detiling, Wii planes/endianness, edge padding and rejection paths\n",checks);
}
