// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "depth_layout.h"
#include <array>
#include <span>
namespace mkw::agc {
// Offsets selected from the native defaults captured by PPSA99566. The
// separate 0x2af record is not a field of SharpProspero's sixteen-entry block.
inline constexpr std::array<uint16_t,16> DepthTargetOffsets{
    0x10,0x11,0x12,0x13,0x14,0x15,0x1a,0x1b,0x1c,0x1d,0x1e,0x2,0x5,0x7,0xb,0xa};
std::array<uint32_t,16> encode_depth_target(std::span<const uint32_t,16> defaults,
    uint32_t width,uint32_t height,uint64_t address);
// D32Float without stencil/HTILE, mode 24. Must remain owned until GPU work
// retires; destruction never waits. This target must not be used with VideoOut.
class GpuDepthTarget {
public:
    GpuDepthTarget(uint32_t width,uint32_t height);
    ~GpuDepthTarget();
    GpuDepthTarget(const GpuDepthTarget&)=delete;
    GpuDepthTarget& operator=(const GpuDepthTarget&)=delete;
    const DepthLayout& layout() const noexcept{return layout_;}
    void* data() const noexcept{return address_;}
    size_t allocation_size() const noexcept{return allocated_;}
    std::array<uint32_t,16> context_values(std::span<const uint32_t,16> defaults) const;
    void clear_after_gpu_idle(float depth);
    int release_after_gpu_idle() noexcept;
private:
    DepthLayout layout_;
    void* address_=nullptr;
    int64_t physical_=-1;
    size_t allocated_=0;
};
}
