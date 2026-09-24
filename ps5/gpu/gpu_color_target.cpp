// SPDX-License-Identifier: GPL-3.0-only
// Direct-memory contract from the requested ps5link-sdk GPU example.
#include "color_target.h"
#include "cpu_cache_flush.h"
#include "gx_perf_stats.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <stdexcept>
extern "C" {
uint64_t sceKernelGetDirectMemorySize();
int sceKernelAllocateDirectMemory(int64_t,int64_t,size_t,size_t,int,int64_t*);
int sceKernelMapDirectMemory(void**,size_t,int,int,int64_t,size_t);
int sceKernelReleaseDirectMemory(int64_t,size_t);
int sceKernelMunmap(void*,size_t);
}
namespace mkw::agc {
namespace {
// steady_clock, not gpu_clock_nanos: host tests link this file without the wait service.
uint64_t target_clock(){return uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());}
struct TargetTiming {
    std::atomic<uint64_t>& count;std::atomic<uint64_t>& nanos;uint64_t started=target_clock();
    ~TargetTiming(){gx_perf_add(nanos,target_clock()-started);gx_perf_add(count,1);}
};
}
GpuColorTarget::GpuColorTarget(uint32_t w,uint32_t h):layout_(w,h,1) {
    TargetTiming timing{gx_perf_stats().targetCreates,gx_perf_stats().targetCreateNanos};
    constexpr size_t alignment=2*1024*1024;
    allocated_=(layout_.byte_size()+alignment-1)&~(alignment-1);
    const uint64_t pool=sceKernelGetDirectMemorySize();
    if(pool>INT64_MAX||allocated_>pool)throw std::runtime_error("Insufficient direct memory for color target");
    try {
        int64_t offset=-1;
        if(sceKernelAllocateDirectMemory(0,int64_t(pool),allocated_,alignment,12,&offset)<0)
            throw std::runtime_error("Color target allocation failed");
        physical_=offset;void* mapped=nullptr;
        if(sceKernelMapDirectMemory(&mapped,allocated_,0x33,0,physical_,alignment)<0)
            throw std::runtime_error("Color target mapping failed");
        address_=mapped;
        if(!address_)throw std::runtime_error("Color target has no mapped address");
        (void)layout_.bgra_descriptor(reinterpret_cast<uintptr_t>(address_));
        clear_after_gpu_idle(0);
    }catch(...){
        if(release_after_gpu_idle())std::fputs("[mkw-agc] Color target rollback failed; kernel retains storage until process cleanup\n",stderr);
        throw;
    }
}
GpuColorTarget::~GpuColorTarget(){
    TargetTiming timing{gx_perf_stats().targetDestroys,gx_perf_stats().targetDestroyNanos};
    if(release_after_gpu_idle())std::fputs("[mkw-agc] Color target release failed; kernel retains storage until process cleanup\n",stderr);
}
std::array<uint32_t,8> GpuColorTarget::texture_descriptor() const {
    if(!address_)throw std::logic_error("Color target is released");
    return layout_.bgra_descriptor(reinterpret_cast<uintptr_t>(address_));
}
std::array<uint32_t,16> GpuColorTarget::context_values(std::span<const uint32_t,16> defaults) const {
    if(!address_)throw std::logic_error("Color target is released");
    return encode_color_target(defaults,layout_.width(),layout_.height(),reinterpret_cast<uintptr_t>(address_));
}
void GpuColorTarget::clear_after_gpu_idle(uint32_t bgra) {
    if(!address_)throw std::logic_error("Color target is released");
    // Only the tiled layout is ever sampled or rendered; the allocation is
    // rounded up to 2 MiB, and clearing and flushing that tail cost ~0.2 ms
    // for every 1x1 clear constant and EFB copy target.
    const size_t bytes=std::min(allocated_,(layout_.byte_size()+63)&~size_t(63));
    std::fill_n(static_cast<uint32_t*>(address_),bytes/4,bgra);
    flush_cpu_cache_lines(address_,bytes);
}
int GpuColorTarget::release_after_gpu_idle() noexcept {
    if(address_){int r=sceKernelMunmap(address_,allocated_);if(r)return r;address_=nullptr;}
    if(physical_>=0){int r=sceKernelReleaseDirectMemory(physical_,allocated_);if(r)return r;physical_=-1;}
    allocated_=0;return 0;
}
}
