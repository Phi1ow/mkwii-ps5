// SPDX-License-Identifier: GPL-3.0-only
#include "gx_copy_color.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <stdexcept>
static unsigned checks;
static void check(bool v){++checks;if(!v)throw std::runtime_error("GX color-copy reference mismatch");}
static unsigned byte(float x){return unsigned(std::lround(x*255));}
int main(){try{
    struct Case{unsigned format;std::array<unsigned,4> result;};
    const Case cases[]={
        {0,{119,119,119,119}},{1,{116,116,116,116}},{2,{119,119,119,85}},{3,{116,116,116,80}},
        {4,{64,128,192,255}},{5,{64,128,192,80}},{6,{64,128,192,80}},
        {0x20,{68,68,68,68}},{0x22,{68,68,68,85}},{0x23,{64,64,64,80}},
        {0x27,{80,80,80,80}},{0x28,{64,64,64,64}},{0x29,{128,128,128,128}},
        {0x2a,{192,192,192,192}},{0x2b,{64,64,64,128}},{0x2c,{128,128,128,192}}
    };
    for(auto t:cases){check(mkw_copy_color_format(t.format));auto c=mkw_copy_convert({64/255.f,128/255.f,192/255.f,80/255.f},t.format);
        check((std::array<unsigned,4>{byte(c.r),byte(c.g),byte(c.b),byte(c.a)}==t.result));}
    for(unsigned format:{7u,8u,14u,0x10u,0x13u,0x16u,0x21u,0x24u,0x2du,0xffffffffu})check(!mkw_copy_color_format(format));
    for(unsigned b=0;b<256;++b){
        check(byte(mkw_copy_saturate(mkw_copy_quantize4(b/255.f)))==(b>>4)*17);
        for(auto weights:{std::array<unsigned,3>{0,64,0},{16,32,16},{8,48,8},{0,0,0},{0,32,0},{64,64,64},{126,189,126},{1,1,1}}){
            unsigned p=b,c=255-b,n=(b*13)&255;
            unsigned expected=std::min(255u,(p*weights[0]+c*weights[1]+n*weights[2])>>6);
            auto filtered=mkw_copy_filter({p/255.f,0,0,.2f},{c/255.f,0,0,.4f},{n/255.f,0,0,.8f},weights[0],weights[1],weights[2]);
            check(byte(filtered.r)==expected);check(filtered.a==.4f);
        }
    }
    std::printf("PASS %u GX copy color checks: 16 formats, nibble quantization, integer vertical-filter oracle and unfiltered alpha\n",checks);return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL %s\n",e.what());return 1;}}
