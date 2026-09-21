// SPDX-License-Identifier: GPL-3.0-only
#include "depth_target.h"
#include <algorithm>
#include <bit>
#include <cmath>
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
GpuDepthTarget::GpuDepthTarget(uint32_t w,uint32_t h):layout_(w,h){
    constexpr size_t alignment=2*1024*1024;
    allocated_=(layout_.byte_size()+alignment-1)&~(alignment-1);
    auto pool=sceKernelGetDirectMemorySize();
    if(pool>INT64_MAX||allocated_>pool)throw std::runtime_error("Insufficient direct memory for depth target");
    try{
        if(sceKernelAllocateDirectMemory(0,int64_t(pool),allocated_,alignment,12,&physical_)<0)
            throw std::runtime_error("Depth allocation failed");
        if(sceKernelMapDirectMemory(&address_,allocated_,0x33,0,physical_,alignment)<0||!address_)
            throw std::runtime_error("Depth mapping failed");
        if((reinterpret_cast<uintptr_t>(address_)&65535)||reinterpret_cast<uintptr_t>(address_)>=(uint64_t{1}<<48))
            throw std::runtime_error("Invalid depth mapping address");
        clear_after_gpu_idle(1);
    }catch(...){if(release_after_gpu_idle())std::fputs("[mkw-depth] rollback failed; allocation retained\n",stderr);throw;}
}
GpuDepthTarget::~GpuDepthTarget(){if(release_after_gpu_idle())std::fputs("[mkw-depth] release failed; allocation retained\n",stderr);}
std::array<uint32_t,16> GpuDepthTarget::context_values(std::span<const uint32_t,16> defaults) const {
    if(!address_)throw std::logic_error("Released depth target");
    return encode_depth_target(defaults,layout_.width(),layout_.height(),reinterpret_cast<uintptr_t>(address_));
}
void GpuDepthTarget::clear_after_gpu_idle(float depth){
    if(!address_||!std::isfinite(depth)||depth<0||depth>1)throw std::invalid_argument("Invalid idle depth clear");
    std::fill_n(static_cast<uint32_t*>(address_),allocated_/4,std::bit_cast<uint32_t>(depth));
    for(size_t i=0;i<allocated_;i+=64)__builtin_ia32_clflush(static_cast<const char*>(address_)+i);
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
}
int GpuDepthTarget::release_after_gpu_idle() noexcept{
    if(address_){auto r=sceKernelMunmap(address_,allocated_);if(r)return r;address_=nullptr;}
    if(physical_>=0){auto r=sceKernelReleaseDirectMemory(physical_,allocated_);if(r)return r;physical_=-1;}
    allocated_=0;return 0;
}
}
