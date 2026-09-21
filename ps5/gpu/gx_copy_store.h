// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "gx_copy_texture_cache.h"
#include <map>
namespace mkw::agc {
// One producer, with guest execution excluded during publication/prepare_write.
// Copies supplied here are complete and immutable, with native-size readback
// storage (either the primary target or its separately prepared downscale).
// Bookkeeping does not own callbacks: Memory independently retains them even
// when GXDestroyCopyTex evicts their sampled texture or this store is destroyed.
class GxCopyStore {
public:
    explicit GxCopyStore(GxCopyTextureCache& cache):cache_(cache){}
    GxCopyStore(const GxCopyStore&)=delete;
    GxCopyStore& operator=(const GxCopyStore&)=delete;
    void publish(uint32_t guestAddress, ColorCopyHandle copy);
    // Bulk HLE stores call this BEFORE writing through a raw host pointer.
    // Materializes untouched bytes and retires every overlapping GPU view.
    void prepare_write(uint32_t guestAddress, size_t bytes);
    size_t tracked_ranges() const noexcept{return ranges_.size();}
private:
    struct Entry {size_t bytes;uint64_t token;const void* destination;};
    GxCopyTextureCache& cache_;
    std::map<uint32_t,Entry> ranges_;
};
GxCopyStore& gx_copy_store();
}
