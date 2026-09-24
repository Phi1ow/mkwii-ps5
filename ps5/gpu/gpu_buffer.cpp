// SPDX-License-Identifier: GPL-3.0-only
#include "gpu_buffer.h"
#include "cpu_cache_flush.h"
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
GpuBuffer::GpuBuffer(size_t bytes){
    constexpr size_t alignment=65536;
    if(!bytes||bytes>SIZE_MAX-(alignment-1))throw std::invalid_argument("Invalid GPU buffer size");
    size_=(bytes+alignment-1)&~(alignment-1);
    const auto pool=sceKernelGetDirectMemorySize();
    if(pool>INT64_MAX||pool<size_)throw std::runtime_error("Insufficient GPU direct memory");
    try{
        if(sceKernelAllocateDirectMemory(0,int64_t(pool),size_,alignment,12,&physical_)<0)throw std::runtime_error("GPU buffer allocation failed");
        if(sceKernelMapDirectMemory(&address_,size_,0x33,0,physical_,alignment)<0||!address_)throw std::runtime_error("GPU buffer mapping failed");
        if((reinterpret_cast<uintptr_t>(address_)&65535)||reinterpret_cast<uintptr_t>(address_)>=(uint64_t{1}<<48))
            throw std::runtime_error("Invalid GPU buffer mapping address");
    }catch(...){if(release_after_gpu_idle())std::fputs("[mkw-gpu-buffer] rollback failed\n",stderr);throw;}
}
GpuBuffer::~GpuBuffer(){if(release_after_gpu_idle())std::fputs("[mkw-gpu-buffer] release failed; retained\n",stderr);}
int GpuBuffer::release_after_gpu_idle() noexcept{
    if(address_){auto r=sceKernelMunmap(address_,size_);if(r)return r;address_=nullptr;}
    if(physical_>=0){auto r=sceKernelReleaseDirectMemory(physical_,size_);if(r)return r;physical_=-1;}
    size_=0;return 0;
}
void GpuBuffer::flush(size_t used) const{
    if(used)flush(0,used);else if(!address_)throw std::invalid_argument("Invalid GPU buffer flush range");
}
void GpuBuffer::flush(size_t offset,size_t bytes) const{
    if(!address_||offset>size_||bytes>size_-offset)throw std::invalid_argument("Invalid GPU buffer flush range");
    flush_cpu_cache_lines(static_cast<const char*>(address_)+offset,bytes);
}
}
