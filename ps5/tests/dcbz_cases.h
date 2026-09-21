// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "memory.h"
#include <cstring>
#include <memory>
#include <stdexcept>
extern "C" int32_t memset_zero_32(int32_t);
inline unsigned test_dcbz_cases(){
    unsigned checks=0;
    auto check=[&](bool b){++checks;if(!b)throw std::runtime_error("dcbz memory regression");};
    struct Pending {uint32_t address;unsigned calls=0;bool fail=false;};
    auto materialize=+[](void* p)->bool{
        auto& state=*static_cast<Pending*>(p);++state.calls;if(state.fail)return false;
        auto* dest=Memory::GetPointer(state.address,96);
        for(unsigned i=0;i<96;++i)dest[i]=uint8_t(i+1);
        return true;
    };
    for(uint32_t bank:{0u,0x10000000u})for(uint32_t reg:{0u,0x80000000u,0xc0000000u})
        for(uint32_t access:{0u,0x80000000u,0xc0000000u}){
            const uint32_t address=bank+reg+0x6000;
            std::memset(Memory::GetPointer(address,96),0xcc,96);
            auto state=std::make_shared<Pending>();state->address=address;
            auto token=Memory::RegisterDeferredRamRead(address,96,materialize,state);
            check(token!=0&&state->calls==0);
            check(memset_zero_32(static_cast<int32_t>(bank+access+0x6020))==0);
            check(state->calls==1&&!Memory::CancelDeferredRead(token));
            const auto* bytes=Memory::GetPointer(address,96);
            for(unsigned i=0;i<96;++i)check(bytes[i]==(i>=32&&i<64?0:uint8_t(i+1)));
            check(Memory::Read32(bank+0xc0006000)==0x01020304);
            check(Memory::Read32(bank+0x80006020)==0);
            check(Memory::Read32(bank+0x6040)==0x41424344);
        }
    // Ordinary mapped RAM still uses one exact 32-byte clear.
    auto* bytes=Memory::GetPointer(0x80006000,96);std::memset(bytes,0x5a,96);
    check(memset_zero_32(static_cast<int32_t>(0xc0006020))==0);
    for(unsigned i=0;i<96;++i)check(bytes[i]==(i>=32&&i<64?0:0x5a));
    // A failed GPU conversion neither clears RAM nor silently retries itself.
    std::memset(bytes,0xcc,96);
    auto state=std::make_shared<Pending>();state->address=0x80006000;state->fail=true;
    auto token=Memory::RegisterDeferredRamRead(state->address,96,materialize,state);
    bool failed=false;
    try{memset_zero_32(static_cast<int32_t>(0xc0006020));}catch(const Memory::AccessViolation&){failed=true;}
    check(failed&&state->calls==1);
    for(unsigned i=0;i<96;++i)check(bytes[i]==0xcc);
    state->fail=false;
    check(memset_zero_32(static_cast<int32_t>(0xc0006020))==0&&state->calls==2);
    for(unsigned i=0;i<96;++i)check(bytes[i]==(i>=32&&i<64?0:uint8_t(i+1)));
    check(!Memory::CancelDeferredRead(token));
    return checks;
}
