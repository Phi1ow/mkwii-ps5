// SPDX-License-Identifier: GPL-3.0-only
// Diagnostic observer callbacks only. The copy cache and material are real.
#include "gx_direct_material.h"
#include "gx/register_backend.hpp"
#include "gx/fifo.hpp"
#include "dolphin/gx/frontend.hpp"
#include "dolphin/gx/__gx.h"
#include <aurora/gfx.h>
#include <dolphin/gx/GXAurora.h>
#include <cstring>
#include <stdexcept>
#ifdef MKW_GX_COPY_READBACK
#include "gx_copy_readback.h"
#include "gx_copy_deferred.h"
#include "gx_copy_store.h"
#include "memory.h"
#include "gx_guest_write.h"
#include <cstdio>
extern "C" int32_t memset_zero_32(int32_t);
#endif
extern "C" void mkw_diagnostic_log(const char*);
#ifdef MKW_GX_COPY_EXECUTE
#include "gx_texture_copy.h"
#ifdef MKW_DEPTH_TEST
#include <bit>
static std::shared_ptr<mkw::agc::GpuDepthTarget> efbDepth;
void mkw_check_gx_depth_clear(){
    if(!efbDepth)throw std::logic_error("Missing diagnostic EFB depth");
    const auto* bytes=static_cast<const char*>(efbDepth->data());
    for(size_t i=0;i<efbDepth->layout().byte_size();i+=64)__builtin_ia32_clflush(bytes+i);
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    for(unsigned y=0;y<1080;++y)for(unsigned x=0;x<1920;++x){
        uint32_t actual;std::memcpy(&actual,bytes+efbDepth->layout().pixel_offset(x,y),4);
        const float expected=x>=240&&x<1680&&y>=135&&y<945?.75f:1.f;
        if(actual!=std::bit_cast<uint32_t>(expected)){
            char line[180];std::snprintf(line,sizeof(line),"[mkw-gx-depth-clear] FAIL %u,%u=%08x expected=%08x\n",x,y,actual,std::bit_cast<uint32_t>(expected));
            mkw_diagnostic_log(line);throw std::runtime_error("GXCopyTex depth clear differs");
        }
    }
    if(efbDepth->release_after_gpu_idle())throw std::runtime_error("EFB depth release failed");
    efbDepth.reset();
    mkw_diagnostic_log("[mkw-gx-depth-clear] PASS 2073600 exact D32 pixels: GX clear 0x400000 -> reversed depth 0.75, scaled rectangle and outside preserved; idle depth target released\n");
}
#endif
mkw::agc::ColorCopyHandle mkw_execute_gx_texture_copy(mkw::agc::AgcBlit& blit,std::shared_ptr<mkw::agc::GpuColorTarget> source){
    using namespace mkw::agc;
    unsigned finishes=0;
#ifdef MKW_DEPTH_TEST
    efbDepth=std::make_shared<GpuDepthTarget>(1920,1080);
#endif
    GxTextureCopyBackend backend(blit,source,{640,360,1920,1080},+[](void* p){++*static_cast<unsigned*>(p);},&finishes
#ifdef MKW_DEPTH_TEST
        ,efbDepth
#endif
    );
    // The preceding diagnostic observes the source GPU completion fence.
    // A complete game renderer must instead drain its own queued draw work.
    set_gx_texture_copy_backend(&backend);
    struct Reset {~Reset(){set_gx_texture_copy_backend(nullptr);}} reset;
    aurora::gx::fifo::init();
    // GXInit normally seeds BP addresses in the SDK's shadow registers. This
    // isolated diagnostic does not initialize the full renderer. Pixel setters
    // preserve the address byte, so a zero-filled shadow writes BP 0 instead.
    __gx->peCtrl=0x43000000;__gx->cmode1=0x42000000;__gx->cmode0=0x41000000;__gx->zmode=0x40000000;
    GXSetTexCopySrc(80,45,480,270);GXSetTexCopyDst(640,360,GX_CTF_RA8,GX_FALSE);
    uint8_t samples[12][2]{};uint8_t filter[7]{};
    GXSetCopyFilter(GX_FALSE,samples,GX_FALSE,filter);
    GXSetPixelFmt(GX_PF_RGBA6_Z24,GX_ZC_LINEAR);GXSetDstAlpha(GX_TRUE,80);
    GXSetColorUpdate(true);GXSetAlphaUpdate(true);GXSetZMode(false,GX_ALWAYS,false);
    GXSetCopyClear(GXColor{16,32,48,64},0xffffff);
#ifdef MKW_DEPTH_TEST
    GXSetZMode(false,GX_ALWAYS,true);GXSetCopyClear(GXColor{16,32,48,64},0x400000);
#endif
    aurora::gx::register_state().viewportPolicy=AURORA_VIEWPORT_FIT;
    void* destination=Memory::GetPointer(0x80200000,640*360*2);
#ifdef MKW_DEPTH_TEST
    aurora::gx::fifo::drain();
    GxTextureCopyBackend missingDepth(blit,source,{640,360,1920,1080},+[](void* p){++*static_cast<unsigned*>(p);},&finishes);
    bool rejected=false;try{missingDepth.execute(destination,aurora::gx::register_state(),true);}catch(const std::logic_error&){rejected=true;}
    if(!rejected||finishes)throw std::logic_error("Missing depth attachment must reject before execution");
    mkw_diagnostic_log("[mkw-gx-depth-clear] PASS missing attachment rejected before synchronizer and GPU modifications\n");
#endif
    GXCopyTex(destination,GX_TRUE);
    if(aurora::gx::register_state().pixelFmt!=GX_PF_RGBA6_Z24||
       aurora::gx::register_state().dstAlpha!=80)
        throw std::logic_error("Copy diagnostic pixel-register initialization differs");
    GXTexObj object{};GXInitTexObj(&object,destination,640,360,GX_TF_IA8,GX_CLAMP,GX_CLAMP,GX_FALSE);
    auto copy=gx_copy_texture_cache().resolve(*reinterpret_cast<GXTexObj_*>(&object));
    if(!copy||finishes!=1||copy->target->layout().width()!=1920||copy->target->layout().height()!=1080||
       !copy->nativeReadback||copy->nativeReadback->layout().width()!=640||copy->nativeReadback->layout().height()!=360)
        throw std::logic_error("GXCopyTex did not publish scaled GPU and native RAM targets");
    mkw_diagnostic_log("[mkw-gx-copy-execute] PASS real GX setters and GXCopyTex: logical crop 80/45/480/270 -> GPU 240/135/1440/810; 1920x1080 sampled copy and 640x360 GPU downscale published\n");
    // Challenge the same GPU fill operation with independent RGB and alpha
    // masks. The retained copy must still contain the pre-clear image.
    blit.clear_color(source,{240,135,720,405},0xff607080,7);
    blit.clear_color(source,{960,540,720,405},0xa0000000,8);
    mkw_diagnostic_log("[mkw-gx-clear] pre-copy alpha, post-copy RGBA rectangle, independent RGB/alpha masks submitted; no source pixel readback\n");
    return copy;
}
#endif
static __GXData_struct shadow{};
__GXData_struct* __gx=&shadow;
namespace aurora {
AuroraConfig g_config{};
std::chrono::nanoseconds wait_for_frame_worker_sealed() noexcept{return {};}
}
namespace aurora::gx {
void set_logical_viewport(const gfx::Viewport&) noexcept{std::terminate();}
void set_logical_scissor(const gfx::ClipRect&) noexcept{std::terminate();}
void set_render_viewport(const gfx::Viewport&) noexcept{std::terminate();}
void set_render_scissor(const gfx::ClipRect&) noexcept{std::terminate();}
namespace fifo {bool handle_draw(u8,const u8*,u32&,u32,bool){throw std::runtime_error("Unexpected game draw in copy binding diagnostic");}}
}
namespace aurora::gfx {
void push_debug_group(std::string){std::terminate();}
void insert_debug_marker(std::string){std::terminate();}
}
extern "C" void aurora_pop_debug_group(){std::terminate();}
static std::unique_ptr<mkw::agc::GxDirectMaterial> retained;
std::array<uint32_t,8> mkw_bind_color_copy(std::shared_ptr<const mkw::agc::GpuColorTarget> target,unsigned copyFormat,unsigned sampleFormat) {
    using namespace mkw::agc;namespace gx=aurora::gx;
    if(retained)throw std::logic_error("Copy binding already active");
    auto& cache=gx_copy_texture_cache();
#ifdef MKW_GX_COPY_READBACK
    void* destination=Memory::GetPointer(0x80200000,640*360*2);
#ifndef MKW_GX_COPY_EXECUTE
    gx_copy_store().publish(0x80200000,std::make_shared<const GxColorCopy>(GxColorCopy{640,360,target,GXTexFmt(copyFormat)}));
    mkw_diagnostic_log("[mkw-copy-store] published completed GPU copy with actual Wii RAM destination before GXLoadTexObj\n");
#endif
#else
    static uint32_t destinationToken; // Non-memory diagnostic modes only.
    void* destination=&destinationToken;
    cache.publish(destination,640,360,GXTexFmt(copyFormat),target);
#endif
    GXTexObj object{};
    GXInitTexObj(&object,destination,640,360,GXTexFmt(sampleFormat),GX_CLAMP,GX_CLAMP,false);
    gx::fifo::init();GXLoadTexObj(&object,GX_TEXMAP2);gx::fifo::drain();
    auto state=gx::register_state();state.numTevStages=1;state.numTexGens=1;state.numIndStages=0;
    for(auto& c:state.colorRegs)c={0,0,0,0};for(auto& c:state.kcolors)c={0,0,0,0};
    auto& stage=state.tevStages[0];stage.texMapId=GX_TEXMAP2;stage.texCoordId=GX_TEXCOORD0;
    stage.colorPass={GX_CC_ZERO,GX_CC_ZERO,GX_CC_ZERO,GX_CC_TEXC};
    stage.alphaPass={GX_CA_ZERO,GX_CA_ZERO,GX_CA_ZERO,GX_CA_TEXA};
    GxTextureCache staticCache;
    retained=std::make_unique<GxDirectMaterial>(GxDirectMaterial::build(state,staticCache,nullptr,nullptr,1));
    if(retained->texture_mask()!=4||staticCache.uploads()!=0||retained->color_copies()[2]->target!=target)
        throw std::logic_error("Material did not retain GPU copy");
    GXDestroyCopyTex(destination);gx::fifo::drain();
    if(cache.size()!=0||retained->color_copies()[2]->target->data()!=target->data())
        throw std::logic_error("GX copy eviction broke material ownership");
    mkw_diagnostic_log("[mkw-copy-binding] real GX load, material GPU binding, zero guest reads/uploads, FIFO eviction and retained owner PASS\n");
    return retained->uniforms().textures[2].texture;
}
#ifdef MKW_GX_COPY_READBACK
extern "C" int mkw_check_color_copy_bytes(){
    try{
        if(!retained||!retained->color_copies()[2])throw std::logic_error("Missing retained typed copy");
        const auto& copy=*retained->color_copies()[2];
        if(copy.format!=GX_CTF_RA8)throw std::logic_error("Copy lost its produced RA8 format");
        auto bytes=mkw::agc::encode_native_color_copy(copy);
        if(bytes.size()!=640*360*2)throw std::logic_error("Unexpected RA8 RAM byte count");
        for(size_t i=0;i<bytes.size();++i)if(bytes[i]!=(i%2?64:80))throw std::runtime_error("RA8 RAM bytes differ");
        mkw_diagnostic_log("[mkw-copy-readback] PASS 460800 exact Wii RA8 bytes (A=80,R=64), after GPU completion and cache eviction\n");
        constexpr uint32_t address=0x80200000, second=0x90200000;
        auto* destination=Memory::GetPointer(address,bytes.size());
        auto generation=GxGuestWrite::GenerationForRange(address,static_cast<uint32_t>(bytes.size()));
        for(size_t i=0;i<bytes.size();++i)if(destination[i]!=0)throw std::logic_error("Deferred copy wrote RAM eagerly");
        if(MemoryInline::FlatRead32(0xc0200000)!=0x50405040)
            throw std::logic_error("Uncached read did not resolve the owned GPU copy");
        if(std::memcmp(destination,bytes.data(),bytes.size())||Memory::Read32(0x00200000)!=0x50405040||
            Memory::Read32(address)!=0x50405040||
            GxGuestWrite::GenerationForRange(address,static_cast<uint32_t>(bytes.size()))<=generation)
            throw std::logic_error("GPU copy RAM aliases, bytes or write generation differ");
        // Execute the actual WiiCompiled dcbz helper without pre-resolving RAM.
        destination=Memory::GetPointer(second,bytes.size());std::memset(destination,0,bytes.size());
        auto& store=mkw::agc::gx_copy_store();
        store.publish(second,retained->color_copies()[2]);
        if(memset_zero_32(static_cast<int32_t>(0xd0200020))!=0)throw std::logic_error("dcbz helper failed");
        for(size_t i=0;i<bytes.size();++i){
            const uint8_t expected=(i>=32&&i<64)?0:bytes[i];
            if(destination[i]!=expected)throw std::logic_error("dcbz lost GPU copy neighbours");
        }
        if(Memory::Read32(0x10200020)!=0||Memory::Read32(second+64)!=0x50405040)
            throw std::logic_error("dcbz result differs across RAM aliases");
        // As with CPU cache writes, GPU visibility changes at cache maintenance.
        // Exercise the existing FIFO eviction explicitly; this is not a call
        // through the game's entire DCStoreRange HLE wrapper.
        GXDestroyCopyTex(destination);aurora::gx::fifo::drain();
        if(mkw::agc::gx_copy_texture_cache().size()!=0)throw std::logic_error("dcbz cache eviction failed");
        store.prepare_write(second,bytes.size());
        store.prepare_write(address,bytes.size());
        if(store.tracked_ranges()!=0)throw std::logic_error("Copy-store ranges did not retire");
        mkw_diagnostic_log("[mkw-gpu-dcbz] PASS actual WiiCompiled helper triggered deferred GPU readback; exactly 32 MEM2 bytes cleared and 460768 neighbouring bytes preserved, aliases verified, FIFO view evicted and all ranges retired\n");
        return 0;
    }catch(const std::exception& e){mkw_diagnostic_log("[mkw-copy-readback] FAIL ");mkw_diagnostic_log(e.what());mkw_diagnostic_log("\n");return 1;}
}
#endif
void mkw_release_color_copy(){retained.reset();mkw_diagnostic_log("[mkw-copy-binding] retired material released\n");}
