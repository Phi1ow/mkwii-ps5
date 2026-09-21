// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstddef>
#include <cstdint>
namespace mkw::agc {
// CPU-visible direct GPU storage. Caller must retire GPU users before release.
class GpuBuffer {
public:
    explicit GpuBuffer(size_t bytes);
    ~GpuBuffer();
    GpuBuffer(const GpuBuffer&)=delete;
    GpuBuffer& operator=(const GpuBuffer&)=delete;
    void* data() const noexcept{return address_;}
    size_t size() const noexcept{return size_;}
    int release_after_gpu_idle() noexcept;
    void flush(size_t used) const;
    // Flush every 64-byte line intersecting [offset, offset+bytes).
    void flush(size_t offset,size_t bytes) const;
private:
    void* address_=nullptr;int64_t physical_=-1;size_t size_=0;
};
}
