// SPDX-License-Identifier: GPL-3.0-only
#include "gx_copy_readback.h"
#include "gx_copy_color.h"
#include "gfx/efb_ram_encoder.hpp"
#include <cstring>
#include <stdexcept>
namespace mkw::agc {
namespace {GxCopyCompletionWait completionWait=nullptr;void* completionContext=nullptr;}
void set_gx_copy_completion_wait(GxCopyCompletionWait wait,void* context){completionWait=wait;completionContext=context;}
std::vector<uint8_t> encode_native_color_copy(const GxColorCopy& copy){
    if(completionWait)completionWait(completionContext);
    namespace ram=aurora::gfx::efb_ram;
    if(!copy.target||!copy.target->data()||(!mkw_copy_color_format(copy.format)&&!mkw_copy_depth_format(copy.format)))
        throw std::invalid_argument("Invalid completed color copy for readback");
    const auto& target=copy.nativeReadback?copy.nativeReadback:copy.target;
    if(!target->data())throw std::invalid_argument("Released native copy readback target");
    const auto& layout=target->layout();
    if(copy.width!=layout.width()||copy.height!=layout.height())
        throw std::invalid_argument("Color copy must be GPU-downscaled to native dimensions before RAM encoding");
    size_t bytes=ram::encoded_size(copy.format,copy.width,copy.height);
    if(!bytes)throw std::invalid_argument("Unsupported Wii RAM copy encoding");
    std::vector<uint8_t> encoded(bytes),linear(size_t(copy.width)*copy.height*4);
    const auto* gpu=static_cast<const uint8_t*>(target->data());
    for(size_t off=0;off<layout.byte_size();off+=64)__builtin_ia32_clflush(gpu+off);
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    for(uint32_t y=0;y<copy.height;++y)for(uint32_t x=0;x<copy.width;++x)
        std::memcpy(linear.data()+(size_t(y)*copy.width+x)*4,gpu+layout.pixel_offset(x,y,0),4);
    if(!ram::encode(encoded.data(),encoded.size(),copy.format,copy.width,copy.height,
        linear.data(),copy.width,copy.height,copy.width*4,ram::HostPixelOrder::BGRA))
        throw std::runtime_error("WiiCompiled color-copy encoder failed");
    return encoded;
}
}
