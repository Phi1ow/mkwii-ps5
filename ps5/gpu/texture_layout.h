// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
namespace mkw::agc {
// RGBA8 2D, one sample, AGC tile mode 27. Wii palette formats are expanded
// by Aurora before upload. Depth, arrays and compressed GPU storage are separate.
class TextureLayout {
public:
    struct Mip { uint32_t width, height, paddedWidth, tailX, tailY; size_t offset; };
    TextureLayout(uint32_t width, uint32_t height, uint32_t mipCount);
    uint32_t width() const { return width_; }
    uint32_t height() const { return height_; }
    uint32_t mip_count() const { return count_; }
    size_t byte_size() const { return bytes_; }
    size_t linear_byte_size() const { return linearBytes_; }
    const Mip& mip(uint32_t level) const;
    size_t pixel_offset(uint32_t x, uint32_t y, uint32_t level) const;
    void tile(std::span<uint8_t> gpuStorage, std::span<const uint8_t> rgbaMips) const;
    std::array<uint32_t,8> descriptor(uint64_t gpuAddress) const;
    // View an existing BGRA8 color target in the same uncompressed tile mode.
    // The caller owns its storage and must retire writes before sampling it.
    std::array<uint32_t,8> bgra_descriptor(uint64_t gpuAddress) const;
private:
    uint32_t width_, height_, count_;
    size_t bytes_=0, linearBytes_=0;
    std::array<Mip,15> mips_{};
};
}
