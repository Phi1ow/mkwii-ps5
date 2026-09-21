// SPDX-License-Identifier: GPL-3.0-only
// Golden Wii texture bytes exercise the actual Aurora decoder, not a second
// software renderer. Same test and fixture generator run on host and PS5.
#include "gfx/texture_convert.hpp"
#include <array>
#include <cstring>
#include <vector>
#include <stdexcept>
extern "C" void mkw_diagnostic_log(const char*);
namespace aurora { AuroraConfig g_config{}; }
namespace {
using namespace aurora;
using namespace aurora::gfx;
unsigned warnings = 0;
void log_callback(AuroraLogLevel level, const char*, const char*, unsigned int) {
    if (level >= LOG_WARNING) ++warnings;
}
void require(bool value) { if (!value) throw std::runtime_error("Wii texture golden vector failed"); }
void pixel(const ConvertedTexture& t, unsigned x, unsigned y, std::array<unsigned char,4> rgba) {
    require(t.format == TextureDataFormat::RGBA8Unorm);
    const size_t offset=(size_t(y)*t.width+x)*4;
    require(offset+4<=t.data.size());
    require(std::memcmp(t.data.data()+offset,rgba.data(),4)==0);
}
ConvertedTexture decode(unsigned format, unsigned w, unsigned h, const std::vector<unsigned char>& data, unsigned mips=1) {
    return convert_texture(format,w,h,mips,ArrayRef<unsigned char>(data));
}
void golden_vectors() {
    std::vector<unsigned char> data(32,0);
    data[0]=0x1e;
    auto i4=decode(GX_TF_I4,8,8,data); pixel(i4,0,0,{17,17,17,17}); pixel(i4,1,0,{238,238,238,238});
    data[0]=0x5b;
    auto i8=decode(GX_TF_I8,8,4,data); pixel(i8,0,0,{91,91,91,91});
    auto ia4=decode(GX_TF_IA4,8,4,data); pixel(ia4,0,0,{187,187,187,85});
    data[0]=0x24; data[1]=0x68;
    auto ia8=decode(GX_TF_IA8,4,4,data); pixel(ia8,0,0,{104,104,104,36});
    data[0]=0xf8;data[1]=0;
    auto rgb565=decode(GX_TF_RGB565,4,4,data); pixel(rgb565,0,0,{255,0,0,255});
    data[0]=0x83;data[1]=0xe0;
    auto opaque=decode(GX_TF_RGB5A3,4,4,data); pixel(opaque,0,0,{0,255,0,255});
    data[0]=0x30;data[1]=0x0f;
    auto alpha=decode(GX_TF_RGB5A3,4,4,data); pixel(alpha,0,0,{0,0,255,109});
    data.assign(64,0);data[0]=0x77;data[1]=0x23;data[32]=0x45;data[33]=0x67;
    auto rgba=decode(GX_TF_RGBA8,4,4,data);pixel(rgba,0,0,{0x23,0x45,0x67,0x77});
    // Four 4x4 compressed sub-blocks. First has red/green endpoints and
    // selectors 0,1,2,3. Second exercises CMPR's transparent midpoint RGB.
    data.assign(32,0); data[0]=0xf8;data[1]=0;data[2]=7;data[3]=0xe0;
    for(int i=4;i<8;++i)data[i]=0x1b;
    data[8]=0;data[9]=0;data[10]=0xff;data[11]=0xff;data[12]=0xff;
    auto cmpr=decode(GX_TF_CMPR,8,8,data);
    pixel(cmpr,0,0,{255,0,0,255});pixel(cmpr,1,0,{0,255,0,255});
    pixel(cmpr,4,0,{127,127,127,0});
    // Palettes: index packing, big-endian 14-bit indices, all TLUT formats,
    // plus out-of-palette entries which Aurora intentionally makes transparent.
    for(unsigned format:{unsigned(GX_TF_C4),unsigned(GX_TF_C8),unsigned(GX_TF_C14X2)}) {
        data.assign(32,0);
        unsigned w=format==GX_TF_C14X2?4:8, h=format==GX_TF_C4?8:4;
        if(format==GX_TF_C4)data[0]=0x12;
        else if(format==GX_TF_C8){data[0]=1;data[1]=2;}
        else {data[0]=0xc0;data[1]=1;data[2]=0;data[3]=2;}
        const std::array<unsigned char,4> pal565={0,0,0xf8,0};
        auto p=convert_texture_palette(format,w,h,1,data,GX_TL_RGB565,2,pal565);
        pixel(p,0,0,{255,0,0,255});pixel(p,1,0,{0,0,0,0});
        const std::array<unsigned char,4> palIa={0,0,0x24,0x68};
        auto q=convert_texture_palette(format,w,h,1,data,GX_TL_IA8,2,palIa);pixel(q,0,0,{104,104,104,36});
        const std::array<unsigned char,4> pal5a={0,0,0x30,0x0f};
        auto r=convert_texture_palette(format,w,h,1,data,GX_TL_RGB5A3,2,pal5a);pixel(r,0,0,{0,0,255,109});
    }
    data.assign(32,0);data[0]=0xf8;
    auto clipped=decode(GX_TF_RGB565,1,1,data);require(clipped.data.size()==4);pixel(clipped,0,0,{255,0,0,255});
    data.assign(64,0);data[0]=0xf8;data[32]=0x07;data[33]=0xe0;
    auto mipped=decode(GX_TF_RGB565,2,2,data,2);require(mipped.mips==2 && mipped.data.size()==20);
    const unsigned char green[4]={0,255,0,255}; require(std::memcmp(mipped.data.data()+16,green,4)==0);
    unsigned before=warnings; data.resize(31);
    require(decode(GX_TF_RGB565,4,4,data).data.empty());require(warnings>before);
    require(decode(GX_TF_RGB565,0,4,data).data.empty());
}
}
extern "C" int mkw_wii_texture_decode(unsigned char* output, unsigned long bytes) {
    aurora::g_config.logCallback=log_callback;
    aurora::g_config.logLevel=LOG_DEBUG;
    try {
        golden_vectors();
        mkw_diagnostic_log("[mkw-wii-texture] PASS direct formats, CMPR, three index formats and TLUTs, small mip and truncated input\n");
        if(!output || bytes<64*64*4) return 2;
        // Wii RGB565, 4x4 tiles, big endian. Four quadrant colours match the
        // AGC readback fixture but are decoded here by real engine code.
        std::vector<unsigned char> encoded(64*64*2);
        size_t at=0;
        for(unsigned by=0;by<64;by+=4)for(unsigned bx=0;bx<64;bx+=4)
            for(unsigned y=0;y<4;++y)for(unsigned x=0;x<4;++x) {
                unsigned value=by<32?(bx<32?0xf800:0x07e0):(bx<32?0x001f:0xffff);
                encoded[at++]=static_cast<unsigned char>(value>>8);encoded[at++]=static_cast<unsigned char>(value);
            }
        auto decoded=decode(GX_TF_RGB565,64,64,encoded);
        require(decoded.data.size()==64*64*4);
        std::memcpy(output,decoded.data.data(),decoded.data.size());
        mkw_diagnostic_log("[mkw-wii-texture] PASS Wii RGB565 fixture decoded for AGC upload\n");
        return 0;
    } catch(const std::exception& e) {
        mkw_diagnostic_log("[mkw-wii-texture] FAIL ");mkw_diagnostic_log(e.what());mkw_diagnostic_log("\n");return 1;
    }
}
#ifdef MKW_HOST_TEXTURE_TEST
#include <cstdio>
extern "C" void mkw_diagnostic_log(const char* text) { std::fputs(text,stdout); }
int main() { std::array<unsigned char,64*64*4> bytes{}; return mkw_wii_texture_decode(bytes.data(),bytes.size()); }
#endif
