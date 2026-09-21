// SPDX-License-Identifier: GPL-3.0-only
// Real GX producers, FIFO decoder, Aurora pixel conversion and AGC tiling.
// Only the physical-memory API is simulated; no GPU rendering is claimed.
#include "gx_texture_cache.h"
#include "hash.hpp"
#include "dolphin/gx/frontend.hpp"
#include "dolphin/gx/__gx.h"
#include "gx/command_processor.hpp"
#include "gx/register_backend.hpp"
#include <aurora/gfx.h>
#include <dolphin/gx/GXAurora.h>
#include <malloc.h>
#include <map>
#include <cstdio>
#include <stdexcept>
#include <exception>
#include <vector>

namespace gx=aurora::gx;
namespace fifo=aurora::gx::fifo;
using mkw::agc::GxTextureCache;
using mkw::agc::TextureHandle;
static __GXData_struct shadow{};
__GXData_struct* __gx=&shadow;
static unsigned checks,allocations,releases;
static bool failMap;
struct Allocation { size_t bytes,alignment; void* address=nullptr; };
static std::map<int64_t,Allocation> physical;
static int64_t nextPhysical=0x100000;
static void require(bool value,const char* label) { ++checks; if(!value) throw std::runtime_error(label); }
extern "C" uint64_t sceKernelGetDirectMemorySize() { return uint64_t{1}<<32; }
extern "C" int sceKernelAllocateDirectMemory(int64_t,int64_t,size_t bytes,size_t align,int type,int64_t* out) {
    require((align==65536||align==2*1024*1024) && type==12 && bytes%align==0,"GPU allocation contract");
    *out=nextPhysical; nextPhysical+=static_cast<int64_t>(bytes);
    physical.emplace(*out,Allocation{bytes,align}); ++allocations; return 0;
}
extern "C" int sceKernelMapDirectMemory(void** out,size_t bytes,int prot,int,int64_t offset,size_t align) {
    require(physical.contains(offset) && physical.at(offset).bytes==bytes && prot==0x33 && align==physical.at(offset).alignment,"GPU mapping contract");
    if(failMap) return -1;
    *out=_aligned_malloc(bytes,align); if(!*out) return -1;
    physical.at(offset).address=*out; return 0;
}
extern "C" int sceKernelMunmap(void* address,size_t bytes) {
    for(auto& [offset,allocation]:physical) if(allocation.address==address) {
        require(allocation.bytes==bytes,"GPU unmap size"); _aligned_free(address); allocation.address=nullptr; return 0;
    }
    throw std::runtime_error("unknown GPU mapping");
}
extern "C" int sceKernelReleaseDirectMemory(int64_t offset,size_t bytes) {
    require(physical.contains(offset) && physical.at(offset).bytes==bytes && !physical.at(offset).address,"GPU release after unmap");
    physical.erase(offset); ++releases; return 0;
}
namespace aurora {
AuroraConfig g_config{};
std::chrono::nanoseconds wait_for_frame_worker_sealed() noexcept { return {}; }
}
// Unused renderer callbacks fail this test if accidentally reached.
namespace aurora::gx {
void set_logical_viewport(const gfx::Viewport&) noexcept { std::terminate(); }
void set_logical_scissor(const gfx::ClipRect&) noexcept { std::terminate(); }
void set_render_viewport(const gfx::Viewport&) noexcept { std::terminate(); }
void set_render_scissor(const gfx::ClipRect&) noexcept { std::terminate(); }
namespace fifo { bool handle_draw(u8,const u8*,u32&,u32,bool) { throw std::runtime_error("unexpected draw"); } }
}
namespace aurora::gfx {
void push_debug_group(std::string) { throw std::runtime_error("unexpected debug group"); }
void insert_debug_marker(std::string) { throw std::runtime_error("unexpected debug marker"); }
}
extern "C" void aurora_pop_debug_group() { std::terminate(); }

static void pixel(const TextureHandle& texture,unsigned x,unsigned y,std::array<u8,4> expected,unsigned level=0) {
    const auto* data=static_cast<const u8*>(texture->gpu.data());
    const auto offset=texture->gpu.layout().pixel_offset(x,y,level);
    require(std::memcmp(data+offset,expected.data(),4)==0,"converted tiled GPU pixel");
}
static GXTexObj_& object(GXTexObj& texture) { return *reinterpret_cast<GXTexObj_*>(&texture); }
static GXTlutObj_& palette_object(GXTlutObj& palette) { return *reinterpret_cast<GXTlutObj_*>(&palette); }
static void fill565(std::span<u8> bytes,u16 value) {
    for(size_t i=0;i<bytes.size();i+=2) { bytes[i]=value>>8; bytes[i+1]=value; }
}
static GXTexObj_ load(GXTexObj& texture) {
    GXLoadTexObj(&texture,GX_TEXMAP4); fifo::drain(); return gx::register_state().loadedTextures[4];
}
static GXTlutObj_ load_palette(GXTlutObj& palette) {
    GXLoadTlut(&palette,GX_TLUT2); fifo::drain(); return gx::register_state().loadedTluts[GX_TLUT2];
}
template<class F> static void rejects(F action,const char* label) {
    bool threw=false; try { action(); } catch(const std::exception&) { threw=true; }
    require(threw,label);
}
static void direct_and_identity() {
    GxTextureCache cache;
    std::vector<u8> data(32); fill565(data,0xf800);
    GXTexObj texture{}; GXInitTexObj(&texture,data.data(),4,4,GX_TF_RGB565,GX_CLAMP,GX_CLAMP,false);
    auto loaded=load(texture);
    auto red=cache.resolve(loaded,data); pixel(red,0,0,{255,0,0,255});
    require(cache.resolve(loaded,data)==red && cache.uploads()==1 && cache.hits()==1,"identical data cache hit");
    for(unsigned i=0;i<6;++i) {
        GXInitTexObj(&texture,data.data(),4,4,GX_TF_RGB565,GX_REPEAT,GX_MIRROR,false);
        GXInvalidateTexAll(); loaded=load(texture);
        require(cache.resolve(loaded,data)==red,"fresh GX IDs and invalidations reuse immutable pixels");
    }
    auto clone=data; GXInitTexObjData(&texture,clone.data()); loaded=load(texture);
    require(cache.resolve(loaded,clone)==red,"same content at different address");
    fill565(clone,0x07e0);
    auto green=cache.resolve(loaded,clone);
    require(green!=red && cache.uploads()==2,"pixel mutation without version update");
    pixel(green,0,0,{0,255,0,255}); pixel(red,0,0,{255,0,0,255});
    const auto count=cache.cached_entries();
    gx::evict_texture_object(loaded.texObjId);
    loaded=gx::register_state().loadedTextures[4];
    require(loaded.no_cache(),"destroyed loaded texture marked no-cache");
    auto transient=cache.resolve(loaded,clone);
    require(transient!=green && cache.cached_entries()==count,"no-cache upload not retained by cache");
    const auto allocated=allocations;
    rejects([&]{cache.resolve(loaded,std::span(clone).first(31));},"truncated texture rejected");
    auto bad=loaded; bad.mFormat=0x7fffffff;
    rejects([&]{cache.resolve(bad,clone);},"unsupported texture format rejected");
    require(allocations==allocated,"invalid data rejected before GPU allocation");
}
static void palettes() {
    GxTextureCache cache;
    std::vector<u8> indices(32,0); indices[0]=0x01;
    std::vector<u8> colors(32,0); colors[0]=0xf8; colors[2]=0x07; colors[3]=0xe0;
    GXTexObj texture{}; GXTlutObj palette{};
    GXInitTexObjCI(&texture,indices.data(),8,8,GX_TF_C4,GX_CLAMP,GX_CLAMP,false,GX_TLUT2);
    GXInitTlutObj(&palette,colors.data(),GX_TL_RGB565,16);
    auto p=load_palette(palette); auto t=load(texture);
    auto first=cache.resolve(t,indices,&p,colors);
    pixel(first,0,0,{255,0,0,255}); pixel(first,1,0,{0,255,0,255});
    colors[0]=0; colors[1]=0x1f;
    auto changed=cache.resolve(t,indices,&p,colors);
    require(changed!=first,"palette mutation without version update"); pixel(changed,0,0,{0,0,255,255});
    const auto oldId=p.tlutObjId,oldVersion=p.tlutDataVersion;
    GXInitTlutObj(&palette,colors.data(),GX_TL_RGB565,16); p=load_palette(palette);
    require(p.tlutObjId==oldId && p.tlutDataVersion!=oldVersion,"GX palette identity tracks changed bytes");
    require(cache.resolve(t,indices,&p,colors)==changed,"new palette version shares already validated content");
    GXDestroyTlutObj(&palette); fifo::drain(); p=gx::register_state().loadedTluts[GX_TLUT2];
    require(p.no_cache(),"FIFO palette destruction sets no-cache");
    const auto count=cache.cached_entries(); auto temporary=cache.resolve(t,indices,&p,colors);
    require(temporary!=changed && cache.cached_entries()==count,"destroyed palette conversion not cached");
    const auto allocated=allocations;
    rejects([&]{cache.resolve(t,indices);},"missing palette rejected");
    rejects([&]{cache.resolve(t,indices,&p,std::span(colors).first(31));},"truncated palette rejected");
    require(allocations==allocated,"bad palette does not allocate GPU storage");
    // Exercise all three index widths through the cache and actual converter.
    for(auto format:{GX_TF_C4,GX_TF_C8,GX_TF_C14X2}) {
        indices.assign(32,0); colors={0x40,0x80};
        const u16 width=format==GX_TF_C14X2 ? 4 : 8;
        const u16 height=format==GX_TF_C4 ? 8 : 4;
        GXInitTexObjCI(&texture,indices.data(),width,height,format,GX_CLAMP,GX_CLAMP,false,GX_TLUT2);
        GXInitTlutObj(&palette,colors.data(),GX_TL_IA8,1); p=load_palette(palette); t=load(texture);
        pixel(cache.resolve(t,indices,&p,colors),0,0,{128,128,128,64});
        colors={0x80,0x1f}; GXInitTlutObj(&palette,colors.data(),GX_TL_RGB5A3,1); p=load_palette(palette);
        pixel(cache.resolve(t,indices,&p,colors),0,0,{0,0,255,255});
    }
}
static void mips_and_linear() {
    GxTextureCache cache; std::vector<u8> data(224); GXTexObj texture{};
    fill565(std::span(data).first(128),0xf800); fill565(std::span(data).subspan(128,32),0x07e0);
    fill565(std::span(data).subspan(160,32),0x001f); fill565(std::span(data).subspan(192,32),0xffff);
    GXInitTexObj(&texture,data.data(),8,8,GX_TF_RGB565,GX_CLAMP,GX_CLAMP,true);
    GXInitTexObjLOD(&texture,GX_NEAR_MIP_NEAR,GX_NEAR,0,3,0,false,false,GX_ANISO_1);
    const auto t=load(texture); require(t.mip_count()==4 && GxTextureCache::source_byte_size(t)==224,"GX mip source extent");
    const auto uploaded=cache.resolve(t,data);
    require(uploaded->hasArbitraryMips,"Aurora authored-mip metadata retained for shader LOD bias");
    pixel(uploaded,0,0,{255,0,0,255},0); pixel(uploaded,0,0,{0,255,0,255},1);
    pixel(uploaded,0,0,{0,0,255,255},2); pixel(uploaded,0,0,{255,255,255,255},3);
    std::vector<u8> rgba{1,2,3,4,5,6,7,8,9,10,11,12};
    GXInitTexObj(&texture,rgba.data(),3,1,GX_TF_RGBA8_PC,GX_CLAMP,GX_CLAMP,false);
    const auto linear=cache.resolve(load(texture),rgba); pixel(linear,2,0,{9,10,11,12});
}
static void lifetime_and_failure() {
    const auto before=physical.size();
    GxTextureCache cache(65536); std::vector<u8> data(32); GXTexObj texture{};
    fill565(data,0xf800); GXInitTexObj(&texture,data.data(),4,4,GX_TF_RGB565,GX_CLAMP,GX_CLAMP,false);
    const auto t=object(texture); auto inFlight=cache.resolve(t,data);
    fill565(data,0x07e0); auto current=cache.resolve(t,data);
    require(cache.cached_entries()==1 && cache.cached_bytes()==65536 && physical.size()==before+2,"LRU evicts cache ownership but retains in-flight storage");
    pixel(inFlight,0,0,{255,0,0,255}); inFlight.reset();
    require(physical.size()==before+1,"retired frame releases evicted texture");
    fill565(data,0x001f); const auto released=releases; failMap=true;
    rejects([&]{cache.resolve(t,data);},"GPU map failure propagated"); failMap=false;
    require(releases==released+1 && physical.size()==before+1 && cache.cached_entries()==1,"failed upload rolled back without destroying previous cache");
    cache.clear(); pixel(current,0,0,{0,255,0,255});
    require(physical.size()==before+1 && cache.cached_bytes()==0,"clear preserves frame ownership");
    current.reset(); require(physical.size()==before,"last owner releases storage");
    GxTextureCache zeroBudget(0); auto transient=zeroBudget.resolve(t,data);
    require(zeroBudget.cached_entries()==0 && zeroBudget.cached_bytes()==0,"oversized/zero-budget textures bypass storage cache");
}
// Digest memo driven by a simulated guest write tracker. The PS5 runtime
// installs WiiCompiled's GxGuestWrite hook; here one counter covers one buffer.
static const void* trackedBase=nullptr;
static size_t trackedSize=0;
static uint64_t trackedGeneration=0;
static bool tracking=true;
static uint64_t fake_generation(const void* data,size_t size) {
    if (!tracking || data!=trackedBase || size>trackedSize) return AURORA_GUEST_WRITE_UNTRACKED;
    return trackedGeneration;
}
static void digest_memo() {
    const auto saved=aurora::g_guestWriteGenerationHook;
    aurora::g_guestWriteGenerationHook=&fake_generation;
    GxTextureCache cache;
    std::vector<u8> data(32); fill565(data,0xf800);
    GXTexObj texture{}; GXInitTexObj(&texture,data.data(),4,4,GX_TF_RGB565,GX_CLAMP,GX_CLAMP,false);
    const auto t=load(texture);
    trackedBase=data.data(); trackedSize=data.size(); trackedGeneration=7; tracking=true;

    auto red=cache.resolve(t,data);
    require(cache.digests()==1 && cache.digest_skips()==0,"first tracked resolve digests once");
    for (unsigned i=0;i<50;++i) require(cache.resolve(t,data)==red,"unchanged tracked source reuses its texture");
    require(cache.digests()==1 && cache.digest_skips()==50,"unchanged generation skips every re-digest");

    // A notified write (DCFlushRange etc.) bumps the generation: re-digest and
    // pick up the new pixels, no version update or invalidation needed.
    fill565(data,0x07e0); ++trackedGeneration;
    auto green=cache.resolve(t,data);
    require(green!=red && cache.digests()==2,"notified write re-digests and uploads new pixels");
    pixel(green,0,0,{0,255,0,255});

    // The memo's only precondition: guest writes must be notified. An
    // un-notified in-place edit keeps the previous digest. Pinning this makes
    // the trust boundary explicit rather than implicit.
    fill565(data,0x001f);
    require(cache.resolve(t,data)==green && cache.digests()==2,"un-notified edit is not detected (documented precondition)");

    // Untracked ranges keep the always-rehash contract and catch the same edit.
    tracking=false;
    auto blue=cache.resolve(t,data);
    require(blue!=green && cache.digests()==3,"untracked source always re-digests");
    pixel(blue,0,0,{0,0,255,255});

    // No-cache slots never use the memo even when tracked.
    tracking=true; ++trackedGeneration;
    (void)cache.resolve(t,data);
    const auto afterTracked=cache.digests();
    auto uncached=t; uncached.set_no_cache(true);
    (void)cache.resolve(uncached,data); (void)cache.resolve(uncached,data);
    require(cache.digests()==afterTracked+2,"no-cache texture digests on every resolve");

    // clear() drops the memo with the entries.
    cache.clear(); const auto beforeClear=cache.digests();
    (void)cache.resolve(t,data);
    require(cache.digests()==beforeClear+1,"clear forgets stored digests");

    aurora::g_guestWriteGenerationHook=saved;
}
void test_gx_materials(const char* fixture);
int main(int argc,char** argv) {
    try {
        aurora::g_config.logCallback=[](AuroraLogLevel,const char*,const char*,unsigned){};
        fifo::init(); direct_and_identity(); palettes(); mips_and_linear(); lifetime_and_failure(); digest_memo();
        if(argc!=2) throw std::runtime_error("Expected SharpProspero sampler fixture path");
        test_gx_materials(argv[1]);
        require(physical.empty() && allocations==releases,"all physical allocations released");
        std::free(fifo::detail::sBufferData); fifo::detail::sBufferData=nullptr;
        std::printf("PASS GX texture cache: %u checks, %u simulated allocations, real GX/FIFO/conversion/tiling, palette changes and frame ownership; simulated memory API\n",checks,allocations);
        return 0;
    } catch(const std::exception& error) { std::fprintf(stderr,"FAIL GX texture cache: %s\n",error.what()); return 1; }
}
