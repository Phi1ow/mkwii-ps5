// SPDX-License-Identifier: GPL-3.0-only
#include "gx_frame_renderer.h"
#include "gx/fifo.hpp"
#include "gx/register_backend.hpp"
#include "../tests/gx_draw_fixture.h"
#include <dolphin/vi.h>
#include <dolphin/gx/frontend.hpp>
#include <cstdio>
#include <cstring>
#include <stdexcept>
extern "C" void mkw_diagnostic_log(const char*);
extern "C" int mkw_test_gx_frame(void* vs,void* ps,void* copyVs,void* copyPs){try{
    using namespace mkw::agc;constexpr uint32_t Base=0xff102030,Red=0xffc04020;
    GXRenderModeObj mode{};mode.viTVmode=VI_TVMODE_PAL_INT;mode.fbWidth=640;mode.efbHeight=528;mode.viWidth=702;mode.viHeight=576;
    VIConfigure(&mode);VISetFrameBufferScale(1);VILockAspectRatio(4,3);AuroraSetViewportPolicy(AURORA_VIEWPORT_FIT);
    GxFrameRenderer frames({vs,ps,copyVs,copyPs});
    struct Binding{GxFrameRenderer* old;~Binding(){exchange_gx_frame_renderer(old);}} binding{exchange_gx_frame_renderer(&frames)};
    auto rejects=[](auto f){bool failed=false;try{f();}catch(const std::logic_error&){failed=true;}if(!failed)throw std::runtime_error("Frame lifecycle misuse accepted");};
    rejects([]{aurora_end_frame();});
    for(unsigned n=0;n<6;++n){
        if(n==2)VISetFrameBufferScale(.5f);
        if(n==3){VISetFrameBufferScale(1);AuroraSetViewportPolicy(AURORA_VIEWPORT_STRETCH);}
        if(n==4){VISetFrameBufferScale(0);AuroraSetViewportPolicy(AURORA_VIEWPORT_FIT);}
        if(n==5){mode.fbWidth=720;mode.efbHeight=576;VIConfigure(&mode);VISetFrameBufferScale(1);}
        auto planned=snapshot_gx_video();auto e=planned.extent;
        auto s=mkw::test::draw_state(e.logicalWidth,e.logicalHeight);s.viewportPolicy=planned.policy;
        s.clearColor={16.f/255,32.f/255,48.f/255,1};s.clearDepth=0x800000;
        aurora::gx::register_state()=s;
        auto queueDraw=[&]{
            auto geometry=mkw::test::draw_geometry(aurora::gx::register_state());std::vector<uint8_t> command{0x80,0,4};command.insert(command.end(),geometry.records().begin(),geometry.records().end());
            aurora::gx::fifo::write_data(command.data(),uint32_t(command.size()));
        };
        // Both startup and a later frame may already have a FIFO prefix.
        // Exact pixel checks below prove it survives begin_frame and executes.
        const bool prequeued=n==0||n==5;
        if(prequeued)queueDraw();
        const auto pending=aurora::gx::fifo::get_buffer_size();
        if(!aurora_begin_frame())throw std::runtime_error("Aurora begin failed");rejects([]{aurora_begin_frame();});
        if(aurora::gx::fifo::get_buffer_size()!=pending)throw std::runtime_error("Frame begin changed pending GX commands");
        u32 rw=0,rh=0,sw=0,sh=0;AuroraGetRenderSize(&rw,&rh);AuroraGetSurfaceSize(&sw,&sh);
        if(rw!=e.targetWidth||rh!=e.targetHeight||sw!=1920||sh!=1080)throw std::runtime_error("VI committed size differs");
        if(n==0){VISetFrameBufferScale(2);AuroraGetRenderSize(&rw,&rh);if(rw!=640||rh!=528)throw std::runtime_error("VI resize changed an active EFB");}
        if(n==1&&(rw!=1280||rh!=1056))throw std::runtime_error("Deferred VI resize was not committed");
        if(n!=4){
            if(!prequeued)queueDraw();
            GXSetDispCopySrc(0,0,u16(e.logicalWidth),u16(e.logicalHeight));GXSetDispCopyDst(u16(e.logicalWidth),u16(e.logicalHeight));
            GXCopyDisp(nullptr,GX_TRUE);
        }
        aurora_end_frame();auto shown=frames.last_presented();rejects([]{aurora_end_frame();});
        if(shown.serial!=n+1)throw std::runtime_error("Aurora end did not present");
        const auto& target=frames.current_target();const auto& r=shown.viewport;
        // Samples stay well away from scaled edges, where linear interpolation
        // intentionally creates intermediate colors. Includes background/bars.
        unsigned checked=0;
        for(unsigned row=0;row<5;++row)for(unsigned col=0;col<5;++col){
            unsigned x=r.x+(2*col+1)*r.width/10,y=r.y+(2*row+1)*r.height/10;
            uint32_t expected=n!=4&&row>0&&row<4&&col>0&&col<4?Red:Base;
            auto* p=static_cast<const char*>(target.data())+target.layout().pixel_offset(x,y,0);__builtin_ia32_clflush(p);__atomic_thread_fence(__ATOMIC_SEQ_CST);
            uint32_t value;std::memcpy(&value,p,4);if(value!=expected)throw std::runtime_error("Aurora frame pixel differs");++checked;
        }
        if(r.x){auto* p=static_cast<const char*>(target.data())+target.layout().pixel_offset(0,540,0);__builtin_ia32_clflush(p);__atomic_thread_fence(__ATOMIC_SEQ_CST);
            uint32_t value;std::memcpy(&value,p,4);if(value!=0xff000000)throw std::runtime_error("VI aspect bar differs");++checked;}
        char line[220];std::snprintf(line,sizeof(line),"[mkw-frame] PASS frame %u EFB %ux%u logical %ux%u viewport %u,%u %ux%u: %u exact stable samples, Aurora begin/end and flip %llu\n",n,rw,rh,e.logicalWidth,e.logicalHeight,r.x,r.y,r.width,r.height,checked,(unsigned long long)shown.count);mkw_diagnostic_log(line);
    }
    if(frames.close()<0)throw std::runtime_error("Frame renderer close failed");rejects([]{aurora_begin_frame();});
    mkw_diagnostic_log("[mkw-frame] PASS prequeued startup/later draws, deferred resize, actual copy producers, blank-frame fallback, six retired flips and close\n");return 0;
}catch(const std::exception& e){mkw_diagnostic_log("[mkw-frame] FAIL ");mkw_diagnostic_log(e.what());mkw_diagnostic_log("\n");return 1;}}
