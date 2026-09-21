// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstdint>
#include <cstddef>
namespace mkw::agc {
// SharpProspero tile mode 24, equation 27: single-sample 2D D32Float.
// One mip and one slice; independent of the mode-27 color-target layout.
class DepthLayout {
public:
    DepthLayout(uint32_t width,uint32_t height);
    uint32_t width() const noexcept{return width_;}
    uint32_t height() const noexcept{return height_;}
    uint32_t padded_width() const noexcept{return paddedWidth_;}
    size_t byte_size() const noexcept{return bytes_;}
    size_t pixel_offset(uint32_t x,uint32_t y) const;
private:
    uint32_t width_,height_,paddedWidth_;
    size_t bytes_;
};
}
