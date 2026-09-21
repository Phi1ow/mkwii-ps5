// SPDX-License-Identifier: GPL-3.0-only
#include "gx_host_memory_ps5.h"
#include <mutex>
#include <set>
namespace mkw::agc {
namespace {
struct Registry {std::mutex mutex;std::set<const GxHostBytes*> owners;};
Registry& registry(){static Registry r;return r;}
}
GxHostBytes::GxHostBytes(){auto& r=registry();std::lock_guard lock(r.mutex);r.owners.insert(this);}
GxHostBytes::~GxHostBytes(){auto& r=registry();std::lock_guard lock(r.mutex);r.owners.erase(this);}
std::span<const uint8_t> find_gx_host_bytes(const void* pointer,size_t size){
    if(!pointer||!size)return {};
    const uintptr_t p=reinterpret_cast<uintptr_t>(pointer);
    auto& r=registry();std::lock_guard lock(r.mutex);
    for(const auto* owner:r.owners){
        const auto& bytes=owner->bytes();const uintptr_t base=reinterpret_cast<uintptr_t>(bytes.data());
        if(p<base)continue;const uintptr_t offset=p-base;
        if(offset<bytes.size()&&size<=bytes.size()-offset)return std::span(bytes).subspan(offset,size);
    }
    return {};
}
}
