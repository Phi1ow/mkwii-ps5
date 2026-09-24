// SPDX-License-Identifier: GPL-3.0-only
// Kernel allocation contract from ps5link-sdk/examples/gpu_cube/main.c.
#include "gpu_texture.h"
#include "cpu_cache_flush.h"
#include <stdexcept>
#include <cstdio>
extern "C" {
uint64_t sceKernelGetDirectMemorySize();
int sceKernelAllocateDirectMemory(int64_t,int64_t,size_t,size_t,int,int64_t*);
int sceKernelMapDirectMemory(void**,size_t,int,int,int64_t,size_t);
int sceKernelReleaseDirectMemory(int64_t,size_t);
int sceKernelMunmap(void*,size_t);
}
namespace mkw::agc {
GpuTexture::GpuTexture(uint32_t w,uint32_t h,uint32_t levels,std::span<const uint8_t> pixels)
:layout_(w,h,levels) {
    if(pixels.size()!=layout_.linear_byte_size())throw std::invalid_argument("RGBA texture mip bytes do not match dimensions");
    allocated_=layout_.byte_size(); // whole 64 KiB tiles already satisfy kernel alignment
    auto pool=sceKernelGetDirectMemorySize();
    if(allocated_>pool || pool>INT64_MAX)throw std::runtime_error("Insufficient GPU direct memory");
    try {
        int64_t candidate=-1;
        if(sceKernelAllocateDirectMemory(0,int64_t(pool),allocated_,65536,12,&candidate)<0)
            throw std::runtime_error("GPU texture allocation failed");
        physical_=candidate;
        void* mapped=nullptr;
        if(sceKernelMapDirectMemory(&mapped,allocated_,0x33,0,physical_,65536)<0)
            throw std::runtime_error("GPU texture mapping failed");
        address_=mapped;
        descriptor_=layout_.descriptor(reinterpret_cast<uintptr_t>(address_));
        layout_.tile({static_cast<uint8_t*>(address_),allocated_},pixels);
        flush_cpu_cache_lines(address_,allocated_);
    }catch(...) {
        if(release_after_gpu_idle())std::fputs("[mkw-agc] GPU texture rollback failed; retained by kernel until process cleanup\n",stderr);
        throw;
    }
}
GpuTexture::~GpuTexture() {
    if(release_after_gpu_idle())std::fputs("[mkw-agc] GPU texture release failed; retained by kernel until process cleanup\n",stderr);
}
int GpuTexture::release_after_gpu_idle() noexcept {
    if(address_) {
        int rc=sceKernelMunmap(address_,allocated_);
        if(rc)return rc; // Do not release physical storage while still mapped.
        address_=nullptr;
    }
    if(physical_>=0) {
        int rc=sceKernelReleaseDirectMemory(physical_,allocated_);
        if(rc)return rc;
        physical_=-1;
    }
    descriptor_.fill(0);allocated_=0;
    return 0;
}
}
