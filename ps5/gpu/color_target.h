// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "texture_layout.h"
namespace mkw::agc {
inline constexpr std::array<uint16_t,16> ColorTargetOffsets{
    0x318,0x31b,0x31c,0x31d,0x31e,0x31f,0x321,0x323,
    0x324,0x325,0x390,0x398,0x3a0,0x3a8,0x3b0,0x3b8};
std::array<uint32_t,16> encode_color_target(std::span<const uint32_t,16> defaults,
    uint32_t width,uint32_t height,uint64_t address);
// Uncompressed BGRA8 target and its matching texture view. Neither destructor
// nor release waits for the GPU: retain the owner through draw retirement AND
// detach it from VideoOut before destruction if used for scan-out.
class GpuColorTarget {
public:
    GpuColorTarget(uint32_t width,uint32_t height);
    ~GpuColorTarget();
    GpuColorTarget(const GpuColorTarget&)=delete;
    GpuColorTarget& operator=(const GpuColorTarget&)=delete;
    void* data() const noexcept{return address_;}
    size_t allocation_size() const noexcept{return allocated_;}
    const TextureLayout& layout() const noexcept{return layout_;}
    std::array<uint32_t,8> texture_descriptor() const;
    std::array<uint32_t,16> context_values(std::span<const uint32_t,16> defaults) const;
    // Initialization or an explicitly idle target only; GPU clears are separate.
    void clear_after_gpu_idle(uint32_t bgra);
    int release_after_gpu_idle() noexcept;
private:
    TextureLayout layout_;
    void* address_=nullptr;
    int64_t physical_=-1;
    size_t allocated_=0;
};
}
