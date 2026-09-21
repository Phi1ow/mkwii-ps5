// SPDX-License-Identifier: GPL-3.0-only
// Isolated diagnostic: real GX objects, cache, converter and direct-memory API.
// Unimplemented frame/draw callbacks below are test-only traps, never game stubs.
#include "gx_direct_material.h"
#include "gx/register_backend.hpp"
#include "gx/command_processor.hpp"
#include "dolphin/gx/frontend.hpp"
#include "dolphin/gx/__gx.h"
#include <aurora/gfx.h>
#include <dolphin/gx/GXAurora.h>
#include <memory>
#include <vector>
#include <cstring>
#include <stdexcept>
extern "C" void mkw_diagnostic_log(const char*);
namespace gx=aurora::gx;
static __GXData_struct shadow{};
__GXData_struct* __gx=&shadow;
namespace aurora {
#ifndef MKW_AURORA_BOOTSTRAP
AuroraConfig g_config{};
#endif
#ifndef MKW_GX_LIFECYCLE
std::chrono::nanoseconds wait_for_frame_worker_sealed() noexcept{return {};}
#endif
}
namespace aurora::gx {
#if !defined(MKW_GX_VIEWPORT) && !defined(MKW_GX_LIFECYCLE)
void set_logical_viewport(const gfx::Viewport&) noexcept{std::terminate();}
void set_logical_scissor(const gfx::ClipRect&) noexcept{std::terminate();}
void set_render_viewport(const gfx::Viewport&) noexcept{std::terminate();}
void set_render_scissor(const gfx::ClipRect&) noexcept{std::terminate();}
#endif
#ifndef MKW_GEOMETRY_PROBE
namespace fifo {bool handle_draw(u8,const u8*,u32&,u32,bool){throw std::runtime_error("Unexpected GX game draw in diagnostic");}}
#endif
}
namespace aurora::gfx {
void push_debug_group(std::string){std::terminate();}
void insert_debug_marker(std::string){std::terminate();}
}
extern "C" void aurora_pop_debug_group(){std::terminate();}
namespace {
std::unique_ptr<mkw::agc::GxDirectMaterial> material;
struct Sources {
    std::vector<uint8_t> rg,blue,palette{0,0,0,31};
    static mkw::agc::GxSourceBytes resolve(void* ctx,const GXTexObj_& t,const GXTlutObj_* p) {
        const auto& s=*static_cast<const Sources*>(ctx);
        if(t.data==s.rg.data() && !p)return {s.rg,{}};
        if(t.data==s.blue.data() && p && p->data==s.palette.data())return {s.blue,s.palette};
        throw std::runtime_error("Unexpected diagnostic texture address");
    }
};
}
extern "C" int mkw_tev_direct_create(void* destination,unsigned long bytes) {
    try {
        if(bytes<sizeof(mkw::agc::GxDirectUniforms) || material)return 1;
        Sources source;
        // Texture A supplies red/green in 4x4 Wii RGB565 tiles.
        for(unsigned by=0;by<64;by+=4)for(unsigned bx=0;bx<64;bx+=4)
            for(unsigned y=0;y<4;++y)for(unsigned x=0;x<4;++x) {
                bool right=bx+x>=32,bottom=by+y>=32;
                unsigned rgb=bottom?(right?0xffe0:0):(right?0x07e0:0xf800);
                source.rg.push_back(rgb>>8);source.rg.push_back(rgb);
            }
        // Texture B supplies blue in the bottom half, with a real C4 TLUT.
        for(unsigned by=0;by<64;by+=8)for(unsigned bx=0;bx<64;bx+=8)
            for(unsigned y=0;y<8;++y)for(unsigned x=0;x<8;x+=2)
                source.blue.push_back(by+y>=32?0x11:0);
        GXTexObj a{},b{};GXTlutObj p{};
        GXInitTexObj(&a,source.rg.data(),64,64,GX_TF_RGB565,GX_CLAMP,GX_CLAMP,false);
        GXInitTexObjCI(&b,source.blue.data(),64,64,GX_TF_C4,GX_CLAMP,GX_CLAMP,false,GX_TLUT2);
#ifdef MKW_GX_FRAGMENT_OUTPUT
        // RGB5A3: black with A=3/7 and blue with A=6/7. Aurora expands these
        // three-bit alpha values to 109 and 219. Different rows share a draw.
        source.palette={0x30,0x00,0x60,0x0f};
        GXInitTlutObj(&p,source.palette.data(),GX_TL_RGB5A3,2);
#else
        GXInitTlutObj(&p,source.palette.data(),GX_TL_RGB565,2);
#endif
        // Exercise the real producers and decoder for loaded texture/TLUT state.
        gx::fifo::init();GXLoadTexObj(&a,GX_TEXMAP2);GXLoadTexObj(&b,GX_TEXMAP7);GXLoadTlut(&p,GX_TLUT2);gx::fifo::drain();
        auto state=gx::register_state();state.numTevStages=2;state.numTexGens=1;state.numIndStages=0;
        for(auto& c:state.colorRegs)c={0,0,0,0};for(auto& c:state.kcolors)c={0,0,0,0};
        state.texCoordScales[0].scaleS=63;state.texCoordScales[0].scaleT=63;
#ifdef MKW_GX_VARYINGS
        state.numTexGens=8;
        for(auto& scale:state.texCoordScales){scale.scaleS=63;scale.scaleT=63;}
#endif
        for(unsigned n=0;n<2;++n) {
            auto& s=state.tevStages[n];s={};s.texMapId=GXTexMapID(n?7:2);s.texCoordId=GX_TEXCOORD0;
            s.channelId=GX_COLOR_ZERO;
            if(n)s.colorPass={GX_CC_TEXC,GX_CC_ZERO,GX_CC_ZERO,GX_CC_CPREV};
            else s.colorPass={GX_CC_ZERO,GX_CC_ZERO,GX_CC_ZERO,GX_CC_TEXC};
            s.alphaPass={GX_CA_ZERO,GX_CA_ZERO,GX_CA_ZERO,GX_CA_TEXA};
#ifdef MKW_GX_LIGHTING
            s.channelId=GX_COLOR0A0;
            s.colorPass={GX_CC_ZERO,GX_CC_TEXC,n?GX_CC_RASA:GX_CC_RASC,n?GX_CC_CPREV:GX_CC_ZERO};
#endif
        }
        mkw::agc::GxTextureCache cache;
        material=std::make_unique<mkw::agc::GxDirectMaterial>(mkw::agc::GxDirectMaterial::build(state,cache,Sources::resolve,&source,1));
        if(material->texture_mask()!=132 || cache.uploads()!=2 || cache.cached_entries()!=2)return 2;
        auto shared=mkw::agc::GxDirectMaterial::build(state,cache,Sources::resolve,&source,1);
        if(cache.hits()!=2 || shared.textures()[2]!=material->textures()[2] || shared.textures()[7]!=material->textures()[7])return 3;
        std::memcpy(destination,&material->uniforms(),sizeof(material->uniforms()));
        cache.clear();
        mkw_diagnostic_log("[mkw-tev-direct] PASS RGB565 and C4/TLUT GX uploads, two cache hits, immutable two-stage material; cache cleared before draw\n");
        return 0;
    }catch(const std::exception& e){mkw_diagnostic_log("[mkw-tev-direct] FAIL ");mkw_diagnostic_log(e.what());mkw_diagnostic_log("\n");return 4;}
}
#ifdef MKW_GX_VARYINGS
extern "C" int mkw_tev_direct_select_frame(void* buffer,unsigned frame) {
#ifdef MKW_GX_BLEND
    if(!material || frame>=72)return 1;
    auto& uniforms=*static_cast<mkw::agc::GxDirectUniforms*>(buffer);
    uniforms.program.alpha_flags=GX_ALWAYS|(GX_ALWAYS<<3);
    for(unsigned n=0;n<2;++n){uniforms.program.stages[n].tex_coord=0;uniforms.program.stages[n].channel=GX_COLOR0A0;}
#elif defined(MKW_GX_FRAGMENT_OUTPUT)
    if(!material || frame>=32)return 1;
    auto& uniforms=*static_cast<mkw::agc::GxDirectUniforms*>(buffer);
    uniforms.program.alpha_flags=(frame&7)|(GX_EQUAL<<3)|((frame/8)<<6)|(128u<<8)|(109u<<16);
    for(unsigned n=0;n<2;++n){uniforms.program.stages[n].tex_coord=0;uniforms.program.stages[n].channel=GX_COLOR0A0;}
#else
    if(!material || frame>=16)return 1;
    auto& uniforms=*static_cast<mkw::agc::GxDirectUniforms*>(buffer);
    for(unsigned n=0;n<2;++n){
        uniforms.program.stages[n].tex_coord=frame&7;
        uniforms.program.stages[n].channel=frame<8?GX_COLOR0A0:GX_COLOR1A1;
    }
#endif
    return 0;
}
#endif
extern "C" int mkw_tev_direct_release() {
    // Call only after a confirmed retired flip. Weak handles verify ownership;
    // GpuTexture reports any kernel unmap/release error independently.
    if(!material)return 1;
    std::weak_ptr a=material->textures()[2],b=material->textures()[7];
    material.reset();
    if(!a.expired() || !b.expired())return 2;
    mkw_diagnostic_log("[mkw-tev-direct] PASS material texture ownership released after retired flip\n");return 0;
}
