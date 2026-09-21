// SPDX-License-Identifier: GPL-3.0-only
#include "agc_gx_draw.h"
#include "gx_renderer.h"
#include "gx_memory_sources.h"
#include "gx_host_memory_ps5.h"
#include "memory.h"
#ifdef MKW_GX_PRESENT
#include "video_presenter.h"
#endif
#ifdef MKW_GX_DISPLAY_COPY
#include "gx_display_copy.h"
static void* displayVs;
static void* displayPs;
extern "C" void mkw_display_copy_shaders(void* vs,void* ps){displayVs=vs;displayPs=ps;}
#endif
#include "gx/register_backend.hpp"
#include "gx/fifo.hpp"
#include "gx/command_processor.hpp"
#include "dolphin/gx/frontend.hpp"
#include "../tests/gx_draw_fixture.h"
#include <bit>
#include <cstdio>
#include <cstring>
#include <stdexcept>
extern "C" void mkw_diagnostic_log(const char*);
unsigned test_gx_memory_sources();
namespace {
using namespace mkw::agc;
constexpr unsigned W=128,H=128,Base=0xff102030,Red=0xffc04020,Green=0xff208040;
std::unique_ptr<AgcGxDraw> draw;
std::shared_ptr<GpuColorTarget> color;
std::shared_ptr<GpuDepthTarget> depth;
struct Sources {
    std::vector<uint8_t> texture,positions;
    static GxSourceBytes tex(void* p,const GXTexObj_& t,const GXTlutObj_* palette){
        auto& s=*static_cast<Sources*>(p);if(t.data!=s.texture.data()||palette)throw std::out_of_range("Unregistered diagnostic texture");
        return {s.texture,{}};
    }
    static std::span<const uint8_t> array(void* p,GXAttr a,const aurora::gx::AttrArray& array,uint32_t offset,uint32_t bytes){
        auto& s=*static_cast<Sources*>(p);
        if(a!=GX_VA_POS||array.data!=s.positions.data()||offset>s.positions.size()||bytes>s.positions.size()-offset)
            throw std::out_of_range("Unregistered diagnostic vertex interval");
        return std::span(s.positions).subspan(offset,bytes);
    }
};
auto texture_state(Sources& source){
    auto s=mkw::test::draw_state(W,H);source.texture.resize(32);
    for(unsigned i=0;i<32;i+=2){source.texture[i]=0;source.texture[i+1]=31;}
    GXTexObj tex{};GXInitTexObj(&tex,source.texture.data(),4,4,GX_TF_RGB565,GX_CLAMP,GX_CLAMP,false);
    s.loadedTextures[0]=*reinterpret_cast<GXTexObj_*>(&tex);s.numTexGens=1;
    s.tcgs[0].type=GX_TG_MTX2x4;s.tcgs[0].src=GX_TG_POS;s.tcgs[0].mtx=GX_IDENTITY;s.tcgs[0].postMtx=GX_PTIDENTITY;
    s.tevStages[0].texMapId=GX_TEXMAP0;s.tevStages[0].texCoordId=GX_TEXCOORD0;
    s.tevStages[0].colorPass.d=GX_CC_TEXC;s.tevStages[0].alphaPass.d=GX_CA_TEXA;
    return s;
}
void reset(){color->clear_after_gpu_idle(Base);depth->clear_after_gpu_idle(0);}
void inspect(const char* phase,uint32_t interior,float z,bool visible=true,bool leftOnly=false){
    // AgcGxDraw records into batches; submit and wait before reading targets.
    if(draw)draw->finish();
    for(const void* p:{color->data(),depth->data()})for(size_t i=0;i<65536;i+=64)__builtin_ia32_clflush(static_cast<const char*>(p)+i);
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x){
        bool in=visible&&x>=16&&x<112&&y>=16&&y<112&&(!leftOnly||x<64);
        uint32_t c,d;std::memcpy(&c,static_cast<const char*>(color->data())+color->layout().pixel_offset(x,y,0),4);
        std::memcpy(&d,static_cast<const char*>(depth->data())+depth->layout().pixel_offset(x,y),4);
        if(c!=(in?interior:Base)||d!=std::bit_cast<uint32_t>(in?z:0.f)){
            char line[200];std::snprintf(line,sizeof(line),"[mkw-gx-draw] FAIL %s %u,%u color=%08x expected=%08x depth=%08x expected=%08x\n",phase,x,y,c,in?interior:Base,d,std::bit_cast<uint32_t>(in?z:0.f));
            mkw_diagnostic_log(line);throw std::runtime_error("GX draw readback differs");
        }
    }
    char line[150];std::snprintf(line,sizeof(line),"[mkw-gx-draw] PASS %s: 16384 exact color and depth pixels\n",phase);mkw_diagnostic_log(line);
}
}
extern "C" int mkw_test_gx_draw(void* vs,void* ps){try{
    using namespace mkw::agc;GxTextureCache cache;
    color=std::make_shared<GpuColorTarget>(W,H);depth=std::make_shared<GpuDepthTarget>(W,H);draw=std::make_unique<AgcGxDraw>(vs,ps);
    reset();auto s=mkw::test::draw_state(W,H);auto g=mkw::test::draw_geometry(s);
    auto packet=GxDrawPacket::build(g,s,cache,nullptr,nullptr);
    s.proj={};s.colorRegs[0]={0,1,0,1}; // Mutation must not affect captured draw.
    draw->draw(std::move(packet),color,depth);inspect("immutable GX matrices/TEV",Red,.75f);
    s=mkw::test::draw_state(W,H);s.colorRegs[0]={32.f/255,128.f/255,64.f/255,1};
    g=mkw::test::draw_geometry(s,.25f);draw->draw(GxDrawPacket::build(g,s,cache,nullptr,nullptr),color,depth);
    inspect("GX LESS reversed-Z occlusion",Red,.75f);
    s.depthFunc=GX_GREATER;draw->draw(GxDrawPacket::build(g,s,cache,nullptr,nullptr),color,depth);
    inspect("GX GREATER reversed-Z overwrite",Green,.25f);
    for(bool reverse:{false,true})for(auto mode:{GX_CULL_FRONT,GX_CULL_BACK}){
        reset();s=mkw::test::draw_state(W,H);s.cullMode=mode;g=mkw::test::draw_geometry(s,.75f,reverse);
        draw->draw(GxDrawPacket::build(g,s,cache,nullptr,nullptr),color,depth);
        inspect(reverse?(mode==GX_CULL_FRONT?"CCW front cull":"CCW back cull"):(mode==GX_CULL_FRONT?"CW front cull":"CW back cull"),Red,.75f,
            reverse?mode==GX_CULL_FRONT:mode==GX_CULL_BACK);
    }
    reset();s=mkw::test::draw_state(W,H);s.renderScissor={0,0,64,128};g=mkw::test::draw_geometry(s);
    draw->draw(GxDrawPacket::build(g,s,cache,nullptr,nullptr),color,depth);inspect("GX scissor",Red,.75f,true,true);
    reset();s=mkw::test::draw_state(W,H);s.alphaCompare.comp0=GX_NEVER;s.alphaCompare.comp1=GX_ALWAYS;s.alphaCompare.op=GX_AOP_AND;
    g=mkw::test::draw_geometry(s);draw->draw(GxDrawPacket::build(g,s,cache,nullptr,nullptr),color,depth);
    inspect("alpha discard preserves depth",Red,.75f,false);
    Sources sources;reset();s=texture_state(sources);g=mkw::test::draw_geometry(s);
    packet=GxDrawPacket::build(g,s,cache,Sources::tex,&sources);
    std::fill(sources.texture.begin(),sources.texture.end(),0);cache.clear();
    draw->draw(std::move(packet),color,depth);inspect("Wii RGB565 after source mutation and cache eviction",0xff0000ff,.75f);
    draw.reset();
    {
        GxRenderer renderer(vs,ps,color,depth,Sources::array,Sources::tex,&sources);
        ScopedGxDrawSink receiver(renderer.sink());
        reset();s=mkw::test::draw_state(W,H);g=mkw::test::draw_geometry(s);sources.positions=g.records();
        s.vtxDesc[GX_VA_POS]=GX_INDEX8;s.arrays[GX_VA_POS]={sources.positions.data(),uint32_t(sources.positions.size()),12,false,{}};
        aurora::gx::register_state()=s;
        const uint8_t indexedQuad[]={0x80,0,4,0,1,2,3};
        aurora::gx::fifo::process(indexedQuad,sizeof(indexedQuad),true);renderer.finish();
        if(renderer.completed_draws()!=1)throw std::runtime_error("FIFO draw did not reach renderer");
        inspect("actual FIFO indexed geometry through renderer",Red,.75f);
        reset();s=texture_state(sources);g=mkw::test::draw_geometry(s);
        aurora::gx::register_state()=s;std::vector<uint8_t> command{0x80,0,4};command.insert(command.end(),g.records().begin(),g.records().end());
        aurora::gx::fifo::process(command.data(),uint32_t(command.size()),true);renderer.finish();renderer.clear_texture_cache();
        if(renderer.completed_draws()!=2)throw std::runtime_error("FIFO textured draw did not reach renderer");
        inspect("actual FIFO textured geometry through renderer",0xff0000ff,.75f);
    }
    {
        char line[160];std::snprintf(line,sizeof(line),"[mkw-gx-memory] PASS %u actual Memory source checks\n",test_gx_memory_sources());mkw_diagnostic_log(line);
        GxRenderer renderer(vs,ps,color,depth,GxMemorySources::array,GxMemorySources::texture,nullptr);
        ScopedGxDrawSink receiver(renderer.sink());
        const uint8_t indexedQuad[]={0x80,0,4,0,1,2,3};
        unsigned completed=0;
        for(uint32_t base:{0u,0x80000000u,0xc0000000u,0x10000000u,0x90000000u,0xd0000000u}){
            reset();s=texture_state(sources);g=mkw::test::draw_geometry(s);
            auto* positions=Memory::GetPointer(base+0x70000,g.records().size());
            std::memcpy(positions,g.records().data(),g.records().size());
            auto* pixels=Memory::GetPointer(0x90071000,32);std::memcpy(pixels,sources.texture.data(),32);
            s.loadedTextures[0].data=pixels;
            s.vtxDesc[GX_VA_POS]=GX_INDEX8;s.arrays[GX_VA_POS]={positions,uint32_t(g.records().size()),12,false,{}};
            aurora::gx::register_state()=s;
            aurora::gx::fifo::process(indexedQuad,sizeof(indexedQuad),true);renderer.finish();
            if(renderer.completed_draws()!=++completed)throw std::runtime_error("Wii RAM draw did not complete");
            std::snprintf(line,sizeof(line),"actual RAM alias %08x indexed vertices and MEM2 texture",base);
            inspect(line,0xff0000ff,.75f);
        }
        struct Pending {
            uint32_t address;std::vector<uint8_t> bytes;unsigned calls=0;
            static bool run(void* p){auto& s=*static_cast<Pending*>(p);++s.calls;
                std::memcpy(Memory::GetPointer(s.address,s.bytes.size()),s.bytes.data(),s.bytes.size());return true;}
        };
        reset();s=texture_state(sources);g=mkw::test::draw_geometry(s);
        auto vertex=std::make_shared<Pending>(Pending{0x80072000,g.records()});
        auto texture=std::make_shared<Pending>(Pending{0x90073000,sources.texture});
        for(auto p:{vertex,texture}){
            std::memset(Memory::GetPointer(p->address,p->bytes.size()),0,p->bytes.size());
            if(!Memory::RegisterDeferredRamRead(p->address,p->bytes.size(),Pending::run,p))throw std::runtime_error("Deferred RAM registration failed");
        }
        s.loadedTextures[0].data=Memory::GetPointer(0xd0073000,32);
        s.vtxDesc[GX_VA_POS]=GX_INDEX8;s.arrays[GX_VA_POS]={Memory::GetPointer(0xc0072000,vertex->bytes.size()),uint32_t(vertex->bytes.size()),12,false,{}};
        aurora::gx::register_state()=s;
        aurora::gx::fifo::process(indexedQuad,sizeof(indexedQuad),true);renderer.finish();
        if(vertex->calls!=1||texture->calls!=1||renderer.completed_draws()!=++completed)throw std::runtime_error("Deferred RAM not resolved exactly once");
        inspect("actual FIFO deferred vertices and texture across RAM aliases",0xff0000ff,.75f);
        reset();GxHostBytes packed;packed.bytes()=g.records();
        s.arrays[GX_VA_POS]={packed.bytes().data(),uint32_t(packed.bytes().size()),12,false,{}};
        aurora::gx::register_state()=s;
        aurora::gx::fifo::process(indexedQuad,sizeof(indexedQuad),true);renderer.finish();
        if(renderer.completed_draws()!=++completed)throw std::runtime_error("Owned host vertices did not complete");
        inspect("actual FIFO owner-tracked host vertices and MEM2 texture",0xff0000ff,.75f);
        renderer.clear_texture_cache();Memory::ClearDeferredReads();
    }
#ifdef MKW_GX_DISPLAY_COPY
    {
        GxRenderer renderer(vs,ps,color,depth,GxMemorySources::array,GxMemorySources::texture,nullptr);
        ScopedGxDrawSink receiver(renderer.sink());
        AgcBlit blitter(displayVs,displayPs,AgcBlit::ShaderAbi::GxCopy);
        GxDisplayCopyBackend copier(blitter,color,depth,{W,H,W,H},[](void* p){static_cast<GxRenderer*>(p)->finish();},&renderer);
        struct Binding {GxDisplayCopyBackend* old;~Binding(){exchange_gx_display_copy_backend(old);}} binding{exchange_gx_display_copy_backend(&copier)};
        auto frame=[&](bool green){
            reset();s=mkw::test::draw_state(W,H);if(green)s.colorRegs[0]={32.f/255,128.f/255,64.f/255,1};
            g=mkw::test::draw_geometry(s);aurora::gx::register_state()=s;
            std::vector<uint8_t> command{0x80,0,4};command.insert(command.end(),g.records().begin(),g.records().end());
            aurora::gx::fifo::process(command.data(),uint32_t(command.size()),true);
            auto& state=aurora::gx::register_state();state.dispCopySrc={0,0,W,H};state.dispCopyDstWidth=W;state.dispCopyDstHeight=H;
            state.viewportPolicy=AURORA_VIEWPORT_NATIVE;state.pixelFmt=GX_PF_RGBA6_Z24;
            state.copyFilterVf=false;state.copyFilterAa=false;state.colorUpdate=true;state.alphaUpdate=true;state.depthUpdate=true;
            state.clearColor={16.f/255,32.f/255,48.f/255,1};state.clearDepth=0x800000;
        };
        auto inspectCopy=[&](std::shared_ptr<const GpuColorTarget> target,uint32_t interior,const char* phase){
            if(!target)throw std::runtime_error("Display copy not published");
            for(size_t i=0;i<target->layout().byte_size();i+=64)__builtin_ia32_clflush(static_cast<const char*>(target->data())+i);
            __atomic_thread_fence(__ATOMIC_SEQ_CST);
            for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x){uint32_t value;
                std::memcpy(&value,static_cast<const char*>(target->data())+target->layout().pixel_offset(x,y,0),4);
                if(value!=(x>=16&&x<112&&y>=16&&y<112?interior:Base))throw std::runtime_error("Display-copy pixels differ");}
            mkw_diagnostic_log("[mkw-display-copy] PASS ");mkw_diagnostic_log(phase);mkw_diagnostic_log(": 16384 exact color pixels\n");
        };
        frame(false);GXCopyDisp(nullptr,GX_TRUE);auto first=copier.latest();inspectCopy(first,Red,"GXCopyDisp after actual FIFO draw and before EFB clear");
        for(const void* p:{color->data(),depth->data()})for(size_t i=0;i<65536;i+=64)__builtin_ia32_clflush(static_cast<const char*>(p)+i);
        __atomic_thread_fence(__ATOMIC_SEQ_CST);
        for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x){uint32_t c,z;
            std::memcpy(&c,static_cast<const char*>(color->data())+color->layout().pixel_offset(x,y,0),4);
            std::memcpy(&z,static_cast<const char*>(depth->data())+depth->layout().pixel_offset(x,y),4);
            if(c!=Base||z!=std::bit_cast<uint32_t>(.5f))throw std::runtime_error("Display-copy EFB clear differs");}
        mkw_diagnostic_log("[mkw-display-copy] PASS EFB color and depth clear: 16384 exact color and depth pixels\n");
        frame(true);GXCopyDisp(nullptr,GX_FALSE);auto second=copier.latest();
        if(second==first)throw std::runtime_error("Display copy overwrote retained frame");
        inspectCopy(second,Green,"second frame after display-copy depth clear");inspectCopy(first,Red,"previous frame preserved while presenter retains it");
        GXCopyDisp(nullptr,GX_FALSE);auto third=copier.latest();bool rejected=false;
        try{GXCopyDisp(nullptr,GX_FALSE);}catch(const std::logic_error&){rejected=true;}
        if(!rejected||copier.latest()!=third)throw std::runtime_error("Busy display pool was overwritten");
        const void* reusable=first->data();first.reset();GXCopyDisp(nullptr,GX_FALSE);
        if(copier.latest()->data()!=reusable)throw std::runtime_error("Retired display target was not reused");
        inspectCopy(copier.latest(),Green,"retired display target reused");
        mkw_diagnostic_log("[mkw-display-copy] PASS three-buffer ownership, backpressure and retirement reuse\n");
#ifdef MKW_GX_PRESENT
        second.reset();third.reset();
        VideoPresenter presenter(blitter);
        for(unsigned n=0;n<8;++n){
            const float aspects[]={1,4.f/3,16.f/9,1};bool green=n&1;
            frame(green);GXCopyDisp(nullptr,GX_TRUE);
            auto shown=presenter.present(copier.latest(),aspects[n%4],false);
            if(shown.serial!=n+1||shown.buffer!=int(n%2))throw std::runtime_error("Presentation serial/buffer differs");
            const auto& scanout=presenter.current_target();
            for(size_t i=0;i<scanout.layout().byte_size();i+=64)__builtin_ia32_clflush(static_cast<const char*>(scanout.data())+i);
            __atomic_thread_fence(__ATOMIC_SEQ_CST);
            const auto& r=shown.viewport;
            // These three aspect ratios put each source edge on an integer
            // display pixel. Nearest sampling gives an exact full-frame oracle.
            for(unsigned y=0;y<1080;++y)for(unsigned x=0;x<1920;++x){
                uint32_t expected=0xff000000,actual;
                if(x>=r.x&&x<r.x+r.width&&y>=r.y&&y<r.y+r.height){
                    bool inside=x>=r.x+r.width/8&&x<r.x+7*r.width/8&&y>=r.y+r.height/8&&y<r.y+7*r.height/8;
                    expected=inside?(green?Green:Red):Base;
                }
                std::memcpy(&actual,static_cast<const char*>(scanout.data())+scanout.layout().pixel_offset(x,y,0),4);
                if(actual!=expected){char error[160];std::snprintf(error,sizeof(error),"[mkw-present] FAIL frame %u pixel %u,%u actual=%08x expected=%08x\n",n,x,y,actual,expected);mkw_diagnostic_log(error);throw std::runtime_error("Scan-out image differs");}
            }
            char line[180];std::snprintf(line,sizeof(line),"[mkw-present] PASS frame %u buffer %d flip %llu viewport %u,%u %ux%u: 2073600 exact scan-out pixels\n",n,shown.buffer,(unsigned long long)shown.count,r.x,r.y,r.width,r.height);mkw_diagnostic_log(line);
        }
        frame(false);GXCopyDisp(nullptr,GX_TRUE);auto shown=presenter.present(copier.latest(),1);
        const auto& linearTarget=presenter.current_target();
        for(auto p:{std::array<unsigned,3>{960,540,Red},{420,0,Base},{500,540,Base},{0,540,0xff000000},{1919,540,0xff000000}}){
            auto* data=static_cast<const char*>(linearTarget.data())+linearTarget.layout().pixel_offset(p[0],p[1],0);
            __builtin_ia32_clflush(data);__atomic_thread_fence(__ATOMIC_SEQ_CST);uint32_t actual;std::memcpy(&actual,data,4);
            if(actual!=p[2])throw std::runtime_error("Linear presentation sample differs");
        }
        if(shown.serial!=9||presenter.close()<0)throw std::runtime_error("VideoOut presenter close failed");
        mkw_diagnostic_log("[mkw-present] PASS linear presentation: five stable samples; nine flips retired; VideoOut closed and scan-out buffers released\n");
#endif
    }
#endif
    draw.reset();if(color->release_after_gpu_idle()||depth->release_after_gpu_idle())throw std::runtime_error("GX draw target release failed");
    color.reset();depth.reset();mkw_diagnostic_log("[mkw-gx-draw] PASS all draws retired; reusable buffers and targets released\n");return 0;
}catch(const std::exception& e){mkw_diagnostic_log("[mkw-gx-draw] FAIL ");mkw_diagnostic_log(e.what());mkw_diagnostic_log("\n");return 1;}}
