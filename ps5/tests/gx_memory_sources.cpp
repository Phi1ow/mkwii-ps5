// SPDX-License-Identifier: GPL-3.0-only
// Actual WiiCompiled Memory and actual source callbacks; only kernel mapping
// is substituted by the existing guest_flat_ps5_test host harness.
#include "gx_memory_sources.h"
#include "gx_host_memory_ps5.h"
#include "memory.h"
#include "dolphin/gx/frontend.hpp"
#include <cstring>
#include <stdexcept>
using namespace mkw::agc;
unsigned test_gx_memory_sources(){
    unsigned checks=0;auto check=[&](bool v){++checks;if(!v)throw std::runtime_error("GX memory source invariant");};
    auto rejects=[&](auto f){bool threw=false;try{f();}catch(const std::exception&){threw=true;}check(threw);};
    for(uint32_t base:{0u,0x80000000u,0xc0000000u,0x10000000u,0x90000000u,0xd0000000u}){
        auto* p=Memory::GetPointer(base+0x60000,32);std::memset(p,0x67,32);
        auto bytes=GxMemorySources::read(p,32);check(bytes.data()==p&&bytes.size()==32&&bytes[31]==0x67);
        aurora::gx::AttrArray array{p,32,4,false,{}};
        auto interval=GxMemorySources::array(nullptr,GX_VA_POS,array,7,13);check(interval.data()==p+7&&interval.size()==13);
        rejects([&]{GxMemorySources::array(nullptr,GX_VA_POS,array,31,2);});
        const auto bankSize=(base==0||base==0x80000000||base==0xc0000000)?Memory::kMem1Size:Memory::kMem2Size;
        auto* end=Memory::GetPointer(base+uint32_t(bankSize)-1,1);check(GxMemorySources::read(end,1).data()==end);
        rejects([&]{GxMemorySources::read(end,2);});
    }
    struct Pending {uint32_t address;unsigned calls=0;bool fail=false;
        static bool run(void* p){auto& s=*static_cast<Pending*>(p);++s.calls;if(s.fail)return false;
            std::memset(Memory::GetPointer(s.address,32),0x39,32);return true;}};
    for(uint32_t address:{0x80061000u,0xd0061000u}){
        auto pending=std::make_shared<Pending>(Pending{address});
        std::memset(Memory::GetPointer(address,32),0,32);
        check(Memory::RegisterDeferredRamRead(address,32,Pending::run,pending)!=0);
        auto p=GxMemorySources::read(Memory::GetPointer(address,32),32);check(p[0]==0x39&&p[31]==0x39&&pending->calls==1);
        GxMemorySources::read(Memory::GetPointer(address,32),32);check(pending->calls==1);
    }
    auto pending=std::make_shared<Pending>(Pending{0x90062000,0,true});
    check(Memory::RegisterDeferredRamRead(pending->address,32,Pending::run,pending)!=0);
    rejects([&]{GxMemorySources::read(Memory::GetPointer(pending->address,32),32);});
    pending->fail=false;auto ready=GxMemorySources::read(Memory::GetPointer(pending->address,32),32);check(ready[0]==0x39&&pending->calls==2);
    const void* oldPointer=nullptr;
    {
        GxHostBytes owner;owner.bytes().resize(32,0x52);oldPointer=owner.bytes().data();
        auto p=GxMemorySources::read(oldPointer,32);check(p[31]==0x52);
        aurora::gx::AttrArray oversized{oldPointer,4096,4,false,{}};
        rejects([&]{GxMemorySources::array(nullptr,GX_VA_POS,oversized,16,32);});
        owner.bytes().reserve(owner.bytes().capacity()+4096);check(owner.bytes().data()!=oldPointer);
        rejects([&]{GxMemorySources::read(oldPointer,1);});
        check(GxMemorySources::read(owner.bytes().data(),32)[31]==0x52);
        owner.bytes().resize(4);rejects([&]{GxMemorySources::read(owner.bytes().data(),5);});
        oldPointer=owner.bytes().data();
    }
    rejects([&]{GxMemorySources::read(oldPointer,1);});
    uint8_t unregistered[32]{};rejects([&]{GxMemorySources::read(unregistered,32);});
    rejects([&]{GxMemorySources::read(nullptr,32);});rejects([&]{GxMemorySources::read(unregistered,0);});
    rejects([&]{GxMemorySources::read(reinterpret_cast<void*>(UINTPTR_MAX-3),32);});
    aurora::gx::AttrArray overflow{reinterpret_cast<void*>(UINTPTR_MAX-3),64,4,false,{}};
    rejects([&]{GxMemorySources::array(nullptr,GX_VA_POS,overflow,8,4);});
    GXTexObj texture{};auto* pixels=Memory::GetPointer(0x90063000,32);
    GXInitTexObj(&texture,pixels,4,4,GX_TF_RGB565,GX_CLAMP,GX_CLAMP,false);
    auto source=GxMemorySources::texture(nullptr,*reinterpret_cast<GXTexObj_*>(&texture),nullptr);
    check(source.texture.data()==pixels&&source.texture.size()==32&&source.palette.empty());
    GXInitTexObjCI(&texture,pixels,8,8,GX_TF_C4,GX_CLAMP,GX_CLAMP,false,GX_TLUT0);
    GXTlutObj_ palette{};palette.data=Memory::GetPointer(0x80064000,32);palette.numEntries=16;palette.format=GX_TL_RGB565;
    pending=std::make_shared<Pending>(Pending{0x80064000});check(Memory::RegisterDeferredRamRead(pending->address,32,Pending::run,pending)!=0);
    source=GxMemorySources::texture(nullptr,*reinterpret_cast<GXTexObj_*>(&texture),&palette);
    check(source.texture.size()==32&&source.palette.size()==32&&source.palette[0]==0x39&&pending->calls==1);
    rejects([&]{GxMemorySources::texture(nullptr,*reinterpret_cast<GXTexObj_*>(&texture),nullptr);});
    palette.numEntries=0;rejects([&]{GxMemorySources::texture(nullptr,*reinterpret_cast<GXTexObj_*>(&texture),&palette);});
    Memory::ClearDeferredReads();return checks;
}
