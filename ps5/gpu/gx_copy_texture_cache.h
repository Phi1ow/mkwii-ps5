// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "color_target.h"
#include "dolphin/gx/texture_object.hpp"
#include <memory>
#include <unordered_map>

namespace mkw::agc {
// A published GPU copy is immutable while any recorded material retains it.
// Logical dimensions identify the Wii texture; the target may be upscaled.
struct GxColorCopy {
    const uint32_t width,height;
    const std::shared_ptr<const GpuColorTarget> target;
    const GXTexFmt format=GX_TF_RGBA8;
    // Optional completed GPU downscale for Wii RAM encoding. The primary
    // target retains its scaled detail for GX texture sampling.
    const std::shared_ptr<const GpuColorTarget> nativeReadback{};
};
using ColorCopyHandle=std::shared_ptr<const GxColorCopy>;
class GxCopyTextureCache {
public:
    // Canonical BGRA8 image already converted by the GX copy shader. format
    // records its Wii storage/sampling identity, not the physical GPU format.
    void publish(const void* destination,uint32_t width,uint32_t height,GXTexFmt format,
                 std::shared_ptr<const GpuColorTarget> target);
    void publish_handle(const void* destination, ColorCopyHandle copy);
    // The caller must first finish the copy's GPU writes. This publishes an
    // existing RGBA8 result, not a GXCopyTex implementation or format converter.
    // Do not modify/release that target until all retained handles retire.
    void publish_rgba8(const void* destination,uint32_t width,uint32_t height,
                      std::shared_ptr<const GpuColorTarget> target);
    ColorCopyHandle resolve(const GXTexObj_&) const;
    void evict(const void* destination) noexcept;
    void clear() noexcept { copies_.clear(); }
    size_t size() const noexcept { return copies_.size(); }
private:
    // Unlike static texture uploads, copies cannot be dropped on an LRU miss:
    // guest RAM may still be stale. Explicit guest-write/death invalidation is
    // required until deferred readback is implemented.
    std::unordered_map<const void*,ColorCopyHandle> copies_;
};
GxCopyTextureCache& gx_copy_texture_cache();
}
