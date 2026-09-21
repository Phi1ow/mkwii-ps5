// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "gpu_texture.h"
#include "dolphin/gx/texture_object.hpp"
#include <array>
#include <memory>
#include <unordered_map>

namespace mkw::agc {
struct GxTexture {
    GpuTexture gpu;
    const bool hasArbitraryMips;
    GxTexture(uint32_t width,uint32_t height,uint32_t levels,std::span<const uint8_t> rgba,bool arbitrary)
        : gpu(width,height,levels,rgba),hasArbitraryMips(arbitrary) {}
};
using TextureHandle = std::shared_ptr<const GxTexture>;
// Cache of static GX textures converted to RGBA8 by Aurora. Each recorded GPU
// draw MUST retain its handle until retirement. Eviction only removes the
// cache's ownership; it cannot invalidate a handle retained by a frame.
// Callers serialize access with renderer_gpu_mutex and keep source bytes
// unchanged during resolve; the uploaded pixels are owned copies. EFB copies
// are separate. The budget limits cache ownership, not in-flight frame memory.
class GxTextureCache {
public:
    explicit GxTextureCache(size_t byteBudget=128u*1024u*1024u) : budget_(byteBudget) {}
    TextureHandle resolve(const GXTexObj_& texture, std::span<const uint8_t> bytes,
                          const GXTlutObj_* palette=nullptr, std::span<const uint8_t> paletteBytes={});
    void clear() noexcept;
    size_t cached_bytes() const noexcept { return used_; }
    size_t cached_entries() const noexcept { return entries_.size(); }
    uint64_t uploads() const noexcept { return uploads_; }
    uint64_t hits() const noexcept { return hits_; }
    // Source digests actually computed versus reused from the memo.
    uint64_t digests() const noexcept { return digests_; }
    uint64_t digest_skips() const noexcept { return digestSkips_; }
    static size_t source_byte_size(const GXTexObj_& texture);
private:
    // Immutable content is shared even across newly created GX object IDs.
    // Sampler fields are intentionally absent: changing wrap/filter/LOD bias
    // must not cause the same pixel storage to be uploaded again.
    struct Key { std::array<uint64_t,5> values{}; bool operator==(const Key&) const = default; };
    struct KeyHash { size_t operator()(const Key& key) const noexcept; };
    struct Entry { TextureHandle texture; uint64_t stamp; };
    std::unordered_map<Key,Entry,KeyHash> entries_;
    size_t budget_, used_=0;
    uint64_t clock_=0, uploads_=0, hits_=0;
    uint64_t next_stamp() noexcept;
    // Digest memo per source address. A stored digest is reused only while
    // the guest write generation of the exact range is unchanged (WiiCompiled
    // GxGuestWrite, notified by DCFlushRange/DCStoreRange/DCInvalidateRange and
    // DMA writers). Unlike aurora it never trusts a GX validation revision, so
    // untracked ranges and no-cache slots keep the previous always-rehash
    // contract: in-place pixel edits are still detected without a version bump.
    struct DigestMemo { size_t size=0; uint64_t digest=0, generation=0; };
    std::unordered_map<const uint8_t*,DigestMemo> memo_;
    uint64_t digests_=0, digestSkips_=0;
    uint64_t source_digest(std::span<const uint8_t> bytes,bool memoize);
};
GxTextureCache& gx_texture_cache();
} // namespace mkw::agc
