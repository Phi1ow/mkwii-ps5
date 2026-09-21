// SPDX-License-Identifier: GPL-3.0-only
#include "gx_texture_cache.h"
#include "gfx/texture_convert.hpp"
#include "gx/register_backend.hpp"
#include "hash.hpp"
#include <algorithm>
#include <stdexcept>

namespace mkw::agc {
namespace {
bool indexed(uint32_t format) {
    return format==GX_TF_C4 || format==GX_TF_C8 || format==GX_TF_C14X2;
}
void validate_format(uint32_t format) {
    switch(format) {
    case GX_TF_I4: case GX_TF_I8: case GX_TF_IA4: case GX_TF_IA8:
    case GX_TF_RGB565: case GX_TF_RGB5A3: case GX_TF_RGBA8: case GX_TF_CMPR:
    case GX_TF_C4: case GX_TF_C8: case GX_TF_C14X2:
    case GX_TF_Z8: case GX_TF_Z16: case GX_TF_Z24X8:
    case GX_TF_R8_PC: case GX_TF_RGBA8_PC: return;
    default: throw std::invalid_argument("Unsupported static GX texture format");
    }
}
uint64_t pair(uint32_t a,uint32_t b) { return (uint64_t(a)<<32)|b; }
constexpr size_t kDigestMemoSoftLimit=4096;
}
uint64_t GxTextureCache::source_digest(std::span<const uint8_t> bytes,bool memoize) {
    // Sample the generation before reading: a write racing the digest bumps
    // the counter, so the next call re-digests instead of trusting stale bytes.
    const uint64_t generation=memoize?aurora::guest_write_generation(bytes.data(),bytes.size())
                                     :aurora::kGuestWriteUntracked;
    if (generation==aurora::kGuestWriteUntracked || bytes.empty()) {
        ++digests_; return aurora::xxh3_hash_s(bytes.data(),bytes.size());
    }
    if (memo_.size()>=kDigestMemoSoftLimit && !memo_.contains(bytes.data())) memo_.clear();
    auto& m=memo_[bytes.data()];
    if (m.size==bytes.size() && aurora::guest_write_generation_matches(m.generation,generation)) {
        ++digestSkips_; return m.digest;
    }
    m.digest=aurora::xxh3_hash_s(bytes.data(),bytes.size());
    m.size=bytes.size(); m.generation=generation; ++digests_;
    return m.digest;
}
size_t GxTextureCache::source_byte_size(const GXTexObj_& texture) {
    validate_format(texture.format());
    // Validates dimensions and mip count before narrowing to GX's u16 API.
    TextureLayout layout(texture.width(),texture.height(),texture.mip_count());
    return GXGetTexBufferSize(static_cast<u16>(layout.width()),static_cast<u16>(layout.height()),
                             texture.format(),layout.mip_count()>1,
                             static_cast<u8>(layout.mip_count()-1));
}
size_t GxTextureCache::KeyHash::operator()(const Key& key) const noexcept {
    return aurora::xxh3_hash(key.values);
}
uint64_t GxTextureCache::next_stamp() noexcept {
    if (++clock_==0) {
        for (auto& [key,entry]:entries_) entry.stamp=0;
        clock_=1;
    }
    return clock_;
}
TextureHandle GxTextureCache::resolve(const GXTexObj_& texture,std::span<const uint8_t> bytes,
                                    const GXTlutObj_* palette,std::span<const uint8_t> paletteBytes) {
    const auto size=source_byte_size(texture);
    if (!texture.data || bytes.size()<size) throw std::invalid_argument("Missing/truncated GX texture bytes");
    bytes=bytes.first(size);
    const bool usePalette=indexed(texture.format());
    if (usePalette) {
        if (!palette || !palette->data || palette->numEntries==0 ||
            (palette->format!=GX_TL_IA8 && palette->format!=GX_TL_RGB565 && palette->format!=GX_TL_RGB5A3))
            throw std::invalid_argument("Missing/invalid GX texture palette");
        const size_t count=size_t(palette->numEntries)*2;
        if (paletteBytes.size()<count) throw std::invalid_argument("Truncated GX texture palette");
        paletteBytes=paletteBytes.first(count);
    }
    Key key;
    key.values[0]=pair(texture.width(),texture.height());
    key.values[1]=pair(texture.format(),texture.mip_count());
    // GX recreates object IDs often and invalidates several times per frame.
    // Always revalidate current bytes; unchanged immutable pixels can be shared
    // across IDs, versions and source addresses without another GPU upload.
    const bool cacheable=!texture.no_cache() && (!usePalette || !palette->no_cache());
    key.values[2]=source_digest(bytes,!texture.no_cache());
    if (usePalette) {
        key.values[3]=pair(palette->format,palette->numEntries);
        key.values[4]=source_digest(paletteBytes,!palette->no_cache());
    }
    if (cacheable) {
        if (auto it=entries_.find(key);it!=entries_.end()) {
            it->second.stamp=next_stamp(); ++hits_; return it->second.texture;
        }
    }
    const TextureLayout layout(texture.width(),texture.height(),texture.mip_count());
    aurora::gfx::ConvertedTexture decoded;
    std::span<const uint8_t> rgba;
    if (texture.format()==GX_TF_RGBA8_PC) {
        rgba=bytes;
    } else {
        aurora::ArrayRef<uint8_t> source(bytes.data(),bytes.size());
        if (usePalette) {
            decoded=aurora::gfx::convert_texture_palette(texture.format(),texture.width(),texture.height(),
                texture.mip_count(),source,palette->format,palette->numEntries,
                aurora::ArrayRef<uint8_t>(paletteBytes.data(),paletteBytes.size()));
        } else {
            decoded=aurora::gfx::convert_texture(texture.format(),texture.width(),texture.height(),texture.mip_count(),source);
        }
        if (decoded.format!=aurora::gfx::TextureDataFormat::RGBA8Unorm || decoded.width!=layout.width() ||
            decoded.height!=layout.height() || decoded.mips!=layout.mip_count())
            throw std::runtime_error("Aurora texture conversion did not produce the requested mip chain");
        rgba={decoded.data.data(),decoded.data.size()};
    }
    if (rgba.size()!=layout.linear_byte_size()) throw std::runtime_error("Wrong converted GX texture size");
    auto uploaded=std::make_shared<GxTexture>(layout.width(),layout.height(),layout.mip_count(),rgba,decoded.hasArbitraryMips);
    ++uploads_;
    if (cacheable && layout.byte_size()<=budget_) {
        const auto [it,inserted]=entries_.emplace(key,Entry{uploaded,next_stamp()});
        if (!inserted) throw std::logic_error("Concurrent GX texture cache access");
        used_+=layout.byte_size();
        while (used_>budget_) {
            const auto oldest=std::min_element(entries_.begin(),entries_.end(),
                [](const auto& a,const auto& b){return a.second.stamp<b.second.stamp;});
            used_-=oldest->second.texture->gpu.layout().byte_size();
            entries_.erase(oldest);
        }
    }
    return uploaded;
}
void GxTextureCache::clear() noexcept { entries_.clear(); memo_.clear(); used_=0; }
GxTextureCache& gx_texture_cache() { static GxTextureCache cache; return cache; }
} // namespace mkw::agc

// No per-object GPU cache exists here: entries own immutable content and each
// resolve re-hashes current source bytes. Match Aurora's loaded-slot no-cache
// semantics on destruction, without throwing away other objects' shared pixels.
namespace aurora::gx {
void evict_texture_object(u32 id) noexcept {
    for (auto& obj:register_state().loadedTextures) if (obj.texObjId==id) obj.set_no_cache(true);
}
void evict_tlut_object(u32 id) noexcept {
    for (auto& obj:register_state().loadedTluts) if (obj.tlutObjId==id) obj.set_no_cache(true);
}
void invalidate_static_texture_cache() noexcept {
    register_state().stateDirty=true;
}
} // namespace aurora::gx
