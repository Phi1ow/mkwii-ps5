// SPDX-License-Identifier: GPL-3.0-only
#include "gx_memory_sources.h"
#include "gx_host_memory_ps5.h"
#include "memory.h"
#include "gx_guest_write.h"
#include <stdexcept>
namespace mkw::agc {
std::span<const uint8_t> GxMemorySources::read(const void* pointer,size_t bytes){
    if(!pointer||!bytes)throw std::invalid_argument("Empty GX source interval");
    struct Bank {uint32_t address;size_t size;};
    constexpr Bank banks[]={{Memory::kMem1PhysicalBase,Memory::kMem1Size},{Memory::kMem1CachedBase,Memory::kMem1Size},
        {Memory::kMem1UncachedBase,Memory::kMem1Size},{Memory::kMem2PhysicalBase,Memory::kMem2Size},
        {Memory::kMem2CachedBase,Memory::kMem2Size},{Memory::kMem2UncachedBase,Memory::kMem2Size}};
    for(const auto& bank:banks){
        if(bytes>bank.size||!Memory::Contains(bank.address,bank.size))continue;
        uint32_t guest;
        if(!GxGuestWrite::HostRangeToGuest(Memory::GetPointer(bank.address,bank.size),bank.size,bank.address,pointer,bytes,guest))continue;
        // A raw host pointer bypasses Memory's guarded read helpers. Resolve
        // pending GPU EFB copies explicitly before exposing these bytes.
        MemoryInline::ResolveDeferredReads(guest,bytes);
        return {Memory::GetPointer(guest,bytes),bytes};
    }
    auto host=find_gx_host_bytes(pointer,bytes);if(!host.empty())return host;
    throw std::out_of_range("GX source is outside live Wii RAM and registered host allocations");
}
std::span<const uint8_t> GxMemorySources::array(void*,GXAttr attr,const aurora::gx::AttrArray& a,uint32_t offset,uint32_t bytes){
    if(unsigned(attr)>=GxGeometryAttributeCount||!a.data||offset>a.size||bytes>a.size-offset)
        throw std::out_of_range("Invalid GX vertex source interval");
    const uintptr_t base=reinterpret_cast<uintptr_t>(a.data);
    if(offset>UINTPTR_MAX-base)throw std::out_of_range("GX vertex address overflow");
    return read(reinterpret_cast<const void*>(base+offset),bytes);
}
GxSourceBytes GxMemorySources::texture(void*,const GXTexObj_& t,const GXTlutObj_* p){
    const auto format=t.format();const bool indexed=format==GX_TF_C4||format==GX_TF_C8||format==GX_TF_C14X2;
    const size_t bytes=GxTextureCache::source_byte_size(t);
    if(indexed&&(!p||!p->data||!p->numEntries||(p->format!=GX_TL_IA8&&p->format!=GX_TL_RGB565&&p->format!=GX_TL_RGB5A3)))
        throw std::invalid_argument("Invalid GX palette source");
    // Resolve both intervals before the material's converter/hash reads either.
    auto pixels=read(t.data,bytes);
    return {pixels,indexed?read(p->data,size_t(p->numEntries)*2):std::span<const uint8_t>{}};
}
}
