// SPDX-License-Identifier: GPL-3.0-only
#include "gx_copy_store.h"
#include "gx_copy_deferred.h"
#include "memory.h"
#include "gx_guest_write.h"
#include "gx_perf_stats.h"
#include "gpu_wait_service.h"
#include <stdexcept>

namespace mkw::agc {
namespace {
struct RamRange {uint32_t physical;size_t bytes;void* host;};
RamRange checked_ram(uint32_t address,size_t bytes) {
    const uint32_t physical=CanonicalizeGxMainRamAddress(address);
    const uint64_t limit=physical<Memory::kMem1Size?Memory::kMem1Size:
        physical>=Memory::kMem2PhysicalBase&&physical<Memory::kMem2PhysicalEnd?Memory::kMem2PhysicalEnd:0;
    if(!limit||!bytes||bytes>limit-physical)throw std::invalid_argument("Copy range outside Wii RAM");
    void* host=nullptr;
    for(uint32_t window:{0u,0x80000000u,0xc0000000u}){
        const uint32_t alias=physical|window;
        if(!Memory::Contains(alias,bytes))throw std::invalid_argument("Copy RAM alias is unmapped");
        void* next=Memory::GetPointer(alias,bytes);
        if(host&&host!=next)throw std::invalid_argument("Copy RAM aliases are not coherent");
        host=next;
    }
    return {physical,bytes,host};
}
bool overlaps(uint32_t a,size_t an,uint32_t b,size_t bn){return uint64_t(a)<uint64_t(b)+bn&&uint64_t(b)<uint64_t(a)+an;}
}
void GxCopyStore::publish(uint32_t address,ColorCopyHandle copy){
    if(!copy)throw std::invalid_argument("Null copy publication");
    const auto incoming=checked_ram(address,native_color_copy_bytes(*copy));
    (void)copy->target->texture_descriptor();
    // Allocate the future map node before retiring anything. Insertion of its
    // extracted node at commit does not allocate.
    std::map<uint32_t,Entry> prepared;
    prepared.emplace(incoming.physical,Entry{incoming.bytes,0,incoming.host});
    auto& stats=gx_perf_stats();
    auto phase=gpu_clock_nanos();
    const auto lap=[&](std::atomic<uint64_t>& counter){const auto now=gpu_clock_nanos();gx_perf_add(counter,now-phase);phase=now;};
    for(const auto& [start,entry]:ranges_){
        if(!overlaps(start,entry.bytes,incoming.physical,incoming.bytes))continue;
        const bool covered=incoming.physical<=start&&uint64_t(incoming.physical)+incoming.bytes>=uint64_t(start)+entry.bytes;
        if(!covered)MemoryInline::ResolveDeferredReads(start,entry.bytes);
    }
    lap(stats.copyOverlapNanos);
    // A fully overwritten old copy needs no readback. Its registration remains
    // intact until the replacement's registration and cache insertion succeed.
    const auto token=defer_native_color_copy(address,copy);
    try{cache_.publish_handle(incoming.host,std::move(copy));}
    catch(...){Memory::CancelDeferredRead(token);throw;}
    lap(stats.copyDeferNanos);
    for(auto it=ranges_.begin();it!=ranges_.end();){
        if(!overlaps(it->first,it->second.bytes,incoming.physical,incoming.bytes)){++it;continue;}
        Memory::CancelDeferredRead(it->second.token);
        if(it->second.destination!=incoming.host)cache_.evict(it->second.destination);
        it=ranges_.erase(it);
    }
    lap(stats.copyRetireNanos);
    auto node=prepared.extract(prepared.begin());node.mapped().token=token;
    ranges_.insert(std::move(node));
}
void GxCopyStore::prepare_write(uint32_t address,size_t bytes){
    if(!bytes)return;
    const auto range=checked_ram(address,bytes);
    MemoryInline::ResolveDeferredReads(range.physical,range.bytes);
    for(auto it=ranges_.begin();it!=ranges_.end();){
        if(!overlaps(it->first,it->second.bytes,range.physical,range.bytes)){++it;continue;}
        cache_.evict(it->second.destination);it=ranges_.erase(it);
    }
}
GxCopyStore& gx_copy_store(){static GxCopyStore store(gx_copy_texture_cache());return store;}
}
