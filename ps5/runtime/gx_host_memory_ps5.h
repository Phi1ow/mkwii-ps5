// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstdint>
#include <span>
#include <vector>
namespace mkw::agc {
// Tracks the real owning vector, not a caller's asserted address/length.
// Use on the guest producer thread: resizing/destruction must be excluded
// while a renderer borrows bytes. Draw packets copy the borrowed data.
class GxHostBytes {
public:
    GxHostBytes();
    ~GxHostBytes();
    GxHostBytes(const GxHostBytes&)=delete;
    GxHostBytes& operator=(const GxHostBytes&)=delete;
    std::vector<uint8_t>& bytes() noexcept{return bytes_;}
    const std::vector<uint8_t>& bytes() const noexcept{return bytes_;}
private:std::vector<uint8_t> bytes_;
};
// Empty means no live registered owner covers the nonempty interval.
std::span<const uint8_t> find_gx_host_bytes(const void*,size_t);
}
