// SPDX-License-Identifier: GPL-3.0-only
#include "gx_copy_texture_cache.h"
#include "gx/register_backend.hpp"
#include "gx_copy_color.h"
#include <stdexcept>
namespace mkw::agc {
void GxCopyTextureCache::publish_rgba8(const void* destination,uint32_t w,uint32_t h,
                                    std::shared_ptr<const GpuColorTarget> target) {
    publish(destination,w,h,GX_TF_RGBA8,std::move(target));
}
void GxCopyTextureCache::publish(const void* destination,uint32_t w,uint32_t h,GXTexFmt format,
                               std::shared_ptr<const GpuColorTarget> target) {
    publish_handle(destination,std::make_shared<const GxColorCopy>(GxColorCopy{w,h,std::move(target),format}));
}
void GxCopyTextureCache::publish_handle(const void* destination, ColorCopyHandle copy) {
    if(!copy)throw std::invalid_argument("Null GX copy handle");
    const auto w=copy->width,h=copy->height;const auto format=copy->format;
    const auto& target=copy->target;
    if(!destination||!w||!h||w>16384||h>16384||!target||!target->data())
        throw std::invalid_argument("Invalid completed GX color copy");
    if(!mkw_copy_color_format(format)&&!mkw_copy_depth_format(format))
        throw std::invalid_argument("Unsupported GX color-copy format");
    // Validate the view before replacing an existing entry.
    (void)target->texture_descriptor();
    copies_.insert_or_assign(destination,std::move(copy));
}
ColorCopyHandle GxCopyTextureCache::resolve(const GXTexObj_& texture) const {
    auto found=copies_.find(texture.data);
    if(found==copies_.end())return {};
    const auto& copy=found->second;
    if(texture.width()!=copy->width||texture.height()!=copy->height||
        !aurora::gx::copy_texture_format_compatible(copy->format,texture.format())||texture.mip_count()!=1)
        throw std::invalid_argument("GX copy reinterpretation/mip conversion requires deferred readback or GPU conversion");
    // A caller violating the ownership contract must never publish a stale
    // descriptor, even if another shared owner kept the C++ object alive.
    (void)copy->target->texture_descriptor();
    return copy;
}
void GxCopyTextureCache::evict(const void* destination) noexcept {copies_.erase(destination);}
GxCopyTextureCache& gx_copy_texture_cache(){static GxCopyTextureCache cache;return cache;}
}
namespace aurora::gx {
void evict_copy_texture(const void* destination) noexcept {
    auto& cache=mkw::agc::gx_copy_texture_cache();
    if(destination)cache.evict(destination);else cache.clear();
    register_state().stateDirty=true;
}
}
