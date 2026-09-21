// SPDX-License-Identifier: GPL-3.0-only
#include "gpu_texture.h"
#include "gfx/texture_convert.hpp"
#include <memory>
#include <vector>
#include <cstring>
extern "C" int mkw_wii_texture_decode(unsigned char*,unsigned long);
extern "C" void mkw_diagnostic_log(const char*);
namespace { std::unique_ptr<mkw::agc::GpuTexture> texture; }
extern "C" int mkw_texture_upload_create(void** address,unsigned long* bytes,unsigned int* descriptor) {
    try {
        std::array<uint8_t,64*64*4> check{};
        if(mkw_wii_texture_decode(check.data(),check.size()))return 1;
        // Odd dimensions and 9 levels cover several 64 KiB blocks plus the
        // mip tail. Base level has quadrants; mip 3 is cyan; other mips magenta.
        std::vector<uint8_t> encoded;
        uint32_t w=257,h=129;
        for(uint32_t level=0;level<9;++level) {
            for(uint32_t by=0;by<h;by+=4)for(uint32_t bx=0;bx<w;bx+=4)
                for(uint32_t y=0;y<4;++y)for(uint32_t x=0;x<4;++x) {
                    uint16_t color=level==3?0x07ff:0xf81f;
                    if(level==0)color=by+y<h/2?(bx+x<w/2?0xf800:0x07e0):(bx+x<w/2?0x001f:0xffff);
                    encoded.push_back(uint8_t(color>>8));encoded.push_back(uint8_t(color));
                }
            w=std::max(1u,w>>1);h=std::max(1u,h>>1);
        }
        auto decoded=aurora::gfx::convert_texture(GX_TF_RGB565,257,129,9,encoded);
        if(decoded.data.empty() || decoded.mips!=9)return 2;
        texture=std::make_unique<mkw::agc::GpuTexture>(257,129,9,std::span(decoded.data.data(),decoded.data.size()));
        *address=const_cast<void*>(texture->data());*bytes=texture->layout().byte_size();
        std::memcpy(descriptor,texture->descriptor().data(),8*sizeof(uint32_t));
        mkw_diagnostic_log("[mkw-upload] PASS general GPU texture upload: 257x129, nine Wii RGB565 mip levels\n");
        return 0;
    }catch(const std::exception& e){mkw_diagnostic_log("[mkw-upload] FAIL ");mkw_diagnostic_log(e.what());mkw_diagnostic_log("\n");return 3;}
}
extern "C" int mkw_texture_upload_release(void) {
    if(!texture)return 0;
    int rc=texture->release_after_gpu_idle();
    if(rc){mkw_diagnostic_log("[mkw-upload] FAIL release after GPU idle\n");return rc;}
    texture.reset();mkw_diagnostic_log("[mkw-upload] PASS GPU texture unmapped and physical storage released after retired flip\n");return 0;
}
