// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "texture_layout.h"
namespace mkw::agc {
// Immutable uploaded texture. The renderer must retain it until all command
// buffers using its descriptor have retired, then release it or its owner.
class GpuTexture {
public:
    GpuTexture(uint32_t width,uint32_t height,uint32_t levels,std::span<const uint8_t> rgbaMips);
    ~GpuTexture();
    GpuTexture(const GpuTexture&)=delete;
    GpuTexture& operator=(const GpuTexture&)=delete;
    const TextureLayout& layout() const {return layout_;}
    const std::array<uint32_t,8>& descriptor() const {return descriptor_;}
    const void* data() const {return address_;}
    int release_after_gpu_idle() noexcept;
private:
    TextureLayout layout_;
    std::array<uint32_t,8> descriptor_{};
    void* address_=nullptr;
    int64_t physical_=-1;
    size_t allocated_=0;
};
}
