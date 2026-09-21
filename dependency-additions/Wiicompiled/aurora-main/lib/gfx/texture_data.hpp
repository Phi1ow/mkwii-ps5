#pragma once
#include "../byte_buffer.hpp"
#include <algorithm>
#include <dolphin/gx.h>
#ifndef MKW_PLATFORM_PS5
#include <webgpu/webgpu_cpp.h>
#endif
namespace aurora::gfx {
// CPU conversion output. PS5 decoding must not pull in a WebGPU device or
// texture object. Other backends retain their existing format ABI.
#ifdef MKW_PLATFORM_PS5
enum class TextureDataFormat { Undefined, RGBA8Unorm, R16Sint };
#else
using TextureDataFormat = wgpu::TextureFormat;
#endif
constexpr uint32_t max_texture_mip_count(uint32_t width, uint32_t height) noexcept {
  uint32_t dimension = std::max(width, height);
  uint32_t count = 1;
  while (dimension > 1) {
    dimension >>= 1;
    ++count;
  }
  return count;
}

} // namespace aurora::gfx
