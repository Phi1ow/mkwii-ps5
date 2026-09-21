// SPDX-License-Identifier: GPL-3.0-only
#include "gx_copy_store.h"
#include "gx_copy_deferred.h"
#include "gx/register_backend.hpp"
#include "memory.h"
#include <cstring>
#include <stdexcept>
using namespace mkw::agc;
// The copy store and deferred readback only time their phases for the frame report.
namespace mkw::agc { uint64_t gpu_clock_nanos() { return 0; } }
unsigned test_gx_copy_store(){
    unsigned checks=0;
    auto check=[&](bool value){++checks;if(!value)throw std::runtime_error("Copy-store regression");};
    auto rejects=[&](auto fn){bool rejected=false;try{fn();}catch(const std::exception&){rejected=true;}check(rejected);};
    auto make=[](uint32_t color,uint32_t width=4){
        auto target=std::make_shared<GpuColorTarget>(width,4);target->clear_after_gpu_idle(color);
        return std::make_shared<const GxColorCopy>(GxColorCopy{width,4,std::move(target),GX_CTF_RA8});
    };
    auto a=make(0x11222222),b=make(0x33444444),c=make(0x55666666);
    auto clear=[](uint32_t address){std::memset(Memory::GetPointer(address,256),0,256);};
    auto inspect=[](uint32_t address){GXTexObj_ t;t.data=Memory::GetPointer(address);t.mWidth=t.mHeight=4;t.mFormat=GX_TF_IA8;return t;};
    auto bytes=[&](uint32_t address,size_t n,uint8_t alpha,uint8_t red){
        auto* data=Memory::GetPointer(address,n);for(size_t i=0;i<n;++i)check(data[i]==(i%2?red:alpha));
    };
    GxCopyTextureCache cache;GxCopyStore store(cache);
    // Exact replacement cancels the first copy without writing any RAM. An
    // already-recorded material handle must continue to own the first image.
    clear(0x80300000);store.publish(0x80300000,a);auto retained=cache.resolve(inspect(0x300000));
    store.publish(0xc0300000,b);check(store.tracked_ranges()==1);
    bytes(0x80300000,32,0,0);check(cache.resolve(inspect(0x80300000))==b&&retained==a);
    check(Memory::Read32(0x300000)==0x33443344);bytes(0x80300000,32,0x33,0x44);
    // A partially overlapping copy preserves both its predecessor's prefix
    // and the new copy's suffix, retiring the predecessor's stale GPU view.
    clear(0x80300100);store.publish(0x80300100,a);store.publish(0x80300110,b);
    check(!cache.resolve(inspect(0x80300100))&&cache.resolve(inspect(0x80300110))==b);
    bytes(0x80300100,32,0x11,0x22);bytes(0x80300120,16,0,0);
    check(Memory::Read32(0xc0300120)==0x33443344);
    bytes(0x80300100,16,0x11,0x22);bytes(0x80300110,32,0x33,0x44);
    // Reverse overlap preserves the tail instead of the prefix.
    clear(0x80300200);store.publish(0x80300210,a);store.publish(0x80300200,b);
    check(Memory::Read32(0x300200)==0x33443344);
    bytes(0x80300200,32,0x33,0x44);bytes(0x80300220,16,0x11,0x22);
    // One copy straddles two independent previous destinations.
    clear(0x90300000);store.publish(0x90300000,a);store.publish(0x90300020,b);store.publish(0xd0300010,c);
    check(Memory::Read32(0x10300010)==0x55665566);
    bytes(0x90300000,16,0x11,0x22);bytes(0x90300010,32,0x55,0x66);bytes(0x90300030,16,0x33,0x44);
    check(!cache.resolve(inspect(0x90300000))&&!cache.resolve(inspect(0x90300020)));
    // Full coverage of two old copies avoids both downloads; a smaller copy
    // entirely inside a larger one preserves the larger prefix AND suffix.
    auto large=make(0x99aaaaaa,8);
    clear(0x90300100);store.publish(0x90300100,a);store.publish(0x90300120,b);
    store.publish(0x90300100,large);bytes(0x90300100,64,0,0);
    check(Memory::Read32(0xd0300100)==0x99aa99aa);bytes(0x90300100,64,0x99,0xaa);
    clear(0x90300200);store.publish(0x90300200,large);store.publish(0x90300210,a);
    bytes(0x90300200,64,0x99,0xaa);
    check(Memory::Read32(0x10300210)==0x11221122);
    bytes(0x90300200,16,0x99,0xaa);bytes(0x90300210,32,0x11,0x22);bytes(0x90300230,16,0x99,0xaa);
    // Texture-cache eviction leaves Memory ownership alive until a guest read.
    clear(0x80300300);auto ephemeral=make(0x77888888);std::weak_ptr<const GxColorCopy> weak=ephemeral;
    store.publish(0x80300300,ephemeral);ephemeral.reset();cache.evict(Memory::GetPointer(0x80300300));
    check(!weak.expired());check(Memory::Read32(0x300300)==0x77887788);check(weak.expired());
    // Raw HLE stores use prepare_write before touching bytes. Unrelated
    // destinations stay lazy and keep their sampled textures.
    clear(0x80300400);store.publish(0x80300400,a);store.publish(0x80300440,b);
    store.prepare_write(0xc0300404,4);
    check(!cache.resolve(inspect(0x80300400))&&cache.resolve(inspect(0x80300440))==b);
    bytes(0x80300440,32,0,0);
    const uint8_t edit[]={0xde,0xad,0xbe,0xef};std::memcpy(Memory::GetPointer(0x80300404,4),edit,4);
    bytes(0x80300400,4,0x11,0x22);bytes(0x80300408,24,0x11,0x22);
    check(Memory::Read32(0x300404)==0xdeadbeef);
    // Reject malformed publication before retiring the existing valid copy.
    clear(0x80300500);store.publish(0x80300500,a);
    rejects([&]{store.publish(0x80300500,{});});
    rejects([&]{store.publish(0x40000000,b);});
    rejects([&]{store.publish(0x817ffff0,b);});
    auto invalid=std::make_shared<const GxColorCopy>(GxColorCopy{8,4,b->target,GX_CTF_RA8});
    rejects([&]{store.publish(0x80300500,invalid);});
    check(cache.resolve(inspect(0x80300500))==a);bytes(0x80300500,32,0,0);
    check(Memory::Read32(0x80300500)==0x11221122);
    // Disjoint preparation doesn't evict or read an unrelated pending copy.
    store.prepare_write(0x80300480,4);bytes(0x80300440,32,0,0);
    check(Memory::Read32(0x80300440)==0x33443344);
    // Sampling retains the scaled GPU image; RAM encoding uses the separate
    // native-sized image. Distinct colors expose accidental source selection.
    auto scaled=std::make_shared<const GxColorCopy>(GxColorCopy{4,4,large->target,GX_CTF_RA8,b->target});
    clear(0x80300600);store.publish(0x80300600,scaled);
    check(cache.resolve(inspect(0x80300600))->target==large->target);
    bytes(0x80300600,32,0,0);
    check(Memory::Read32(0xc0300600)==0x33443344);bytes(0x80300600,32,0x33,0x44);
    auto badNative=std::make_shared<const GxColorCopy>(GxColorCopy{4,4,large->target,GX_CTF_RA8,large->target});
    rejects([&]{store.publish(0x80300600,badNative);});
    check(cache.resolve(inspect(0x80300600))==scaled);
    Memory::ClearDeferredReads();cache.clear();
    return checks;
}
