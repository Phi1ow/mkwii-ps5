// SPDX-License-Identifier: GPL-3.0-only
#include "gx_frame_renderer.h"
#include "gpu_wait_service.h"
#include <aurora/gfx.h>
#include "../tests/gx_draw_fixture.h"
#include "gx/fifo.hpp"
#include "gx/register_backend.hpp"
#include <dolphin/vi.h>
#include <dolphin/pad.h>
#include <aurora/event.h>
#include <dolphin/gx/frontend.hpp>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <atomic>
#include <thread>
#include <mutex>
#include <cmath>
extern "C" void mkw_diagnostic_log(const char*);
extern "C" int sceKernelUsleep(unsigned);
namespace {uint64_t timingCallbacks=0;bool reentryBlocked=false;
void serviceTiming(){++timingCallbacks;if(!reentryBlocked){try{aurora_begin_frame();}catch(const std::logic_error& e){reentryBlocked=std::strstr(e.what(),"cannot reenter Aurora")!=nullptr;}}}}
extern "C" int mkw_test_aurora_bootstrap(){try{
    using namespace mkw::agc;
    auto check=[](bool b){if(!b)throw std::runtime_error("Aurora bootstrap expectation failed");};
    auto rejects=[&](auto f){bool rejected=false;try{f();}catch(const std::exception&){rejected=true;}check(rejected);};
    check(aurora_get_backend()==BACKEND_NULL);aurora_shutdown();rejects([]{aurora_update();});
    rejects([]{aurora_initialize(0,nullptr,nullptr);});
    AuroraConfig config{};config.desiredBackend=BACKEND_AGC;config.resourcesPath="/app0/missing-shader-test";
    rejects([&]{aurora_initialize(0,nullptr,&config);});check(!current_gx_frame_renderer());
    config.resourcesPath="/app0";config.msaa=4;rejects([&]{aurora_initialize(0,nullptr,&config);});config.msaa=0;
    // Configuration before initialization must survive the bootstrap, as in
    // runtime/main.cpp ConfigureMkwDynamicAspect + VISetFrameBufferScale.
    VIConfigure(nullptr);VISetFrameBufferScale(1);VILockAspectRatio(4,3);AuroraSetViewportPolicy(AURORA_VIEWPORT_FIT);
    for(unsigned cycle=0;cycle<2;++cycle){
        std::string app="Bootstrap owned string",user="/data/wiicompiled",cache="/data/wiicompiled/Cache";
        config.appName=app.c_str();config.userPath=user.c_str();config.cachePath=cache.c_str();
        auto info=aurora_initialize(0,nullptr,&config);
        // Use the real native polling service through the public Aurora API.
        // No synthetic joystick or replacement Pad service is linked here.
        check(aurora_update()!=nullptr);
        app.clear();user.clear();cache.clear();
        check(info.backend==BACKEND_AGC&&aurora_get_backend()==BACKEND_AGC&&!info.window);
        check(info.windowSize.native_fb_width==1920&&info.windowSize.native_fb_height==1080&&info.windowSize.fb_width==640&&info.windowSize.fb_height==528);
        check(std::strcmp(info.userPath,"/data/wiicompiled")==0&&std::strcmp(info.cachePath,"/data/wiicompiled/Cache")==0);
        rejects([&]{aurora_initialize(0,nullptr,&config);});
        size_t count=0;check(aurora_get_available_backends(&count)[0]==BACKEND_AGC&&count==1);
        aurora_set_disable_copy_filter(cycle==0);
        auto* renderer=current_gx_frame_renderer();check(renderer!=nullptr);
        AuroraPresentTiming timing{};aurora_get_present_timing(&timing);check(timing.totalPresentCount==0&&timing.sampleCount==0);
        timingCallbacks=0;reentryBlocked=false;aurora_set_frame_worker_wait_callback(serviceTiming);
        check(aurora_wait_for_frame_worker_for(0));aurora_wait_for_frame_worker();
        aurora_set_frame_interpolation_fps(0);rejects([]{aurora_set_frame_interpolation_fps(120);});
        check(aurora_get_frame_interpolation_fps()==0&&aurora_get_queued_pipeline_count()==0);
        aurora_set_skip_unready_pipelines(true);check(aurora_get_skip_unready_pipelines());
        rejects([]{aurora_set_present_schedule(1,2);});
        std::atomic<bool> held=false;
        std::thread locker([&]{std::lock_guard lock(aurora::renderer_gpu_mutex());held.store(true,std::memory_order_release);sceKernelUsleep(20000);});
        while(!held.load(std::memory_order_acquire))gpu_wait_slice(100);
        bool timedOut=false;try{timedOut=!aurora_wait_for_frame_worker_for(1000);}catch(...){locker.join();throw;}
        locker.join();check(timedOut);check(aurora_wait_for_frame_worker_for(0));
        for(unsigned n=0;n<2;++n){
            auto state=mkw::test::draw_state(640,528);state.viewportPolicy=AURORA_VIEWPORT_FIT;
            state.clearColor={16.f/255,32.f/255,48.f/255,1};state.clearDepth=0x800000;aurora::gx::register_state()=state;
            check(aurora_begin_frame());
            if(n==0){auto geometry=mkw::test::draw_geometry(state);std::vector<uint8_t> command{0x80,0,4};command.insert(command.end(),geometry.records().begin(),geometry.records().end());
                aurora::gx::fifo::write_data(command.data(),uint32_t(command.size()));GXSetDispCopySrc(0,0,640,528);GXSetDispCopyDst(640,528);GXCopyDisp(nullptr,GX_TRUE);}
            uint64_t deadline=0;if(n==0){deadline=gpu_clock_nanos()+80000000;aurora_report_producer_paced(true);aurora_set_present_schedule(deadline+16666667-6500000,16666667);}else aurora_set_present_schedule(0,0);
            aurora_end_frame();check(renderer->last_presented().serial==n+1);check(gpu_clock_nanos()>=deadline);
            aurora_get_present_timing(&timing);check(timing.totalPresentCount==n+1&&timing.sampleCount==n);
            if(n)check(timing.averageFrameTimeMs>0&&std::isfinite(timing.framesPerSecond)&&timing.framesPerSecond>0&&timing.effectiveFramesPerSecond==timing.framesPerSecond);
            aurora_wait_for_frame_worker();
            const auto& target=renderer->current_target();
            for(auto xy:std::array<std::array<unsigned,2>,3>{{{960,540},{250,10},{0,540}}}){
                auto* p=static_cast<const char*>(target.data())+target.layout().pixel_offset(xy[0],xy[1],0);__builtin_ia32_clflush(p);__atomic_thread_fence(__ATOMIC_SEQ_CST);
                uint32_t actual;std::memcpy(&actual,p,4);check(actual==(xy[0]==0?0xff000000:xy[0]==960&&n==0?0xffc04020:0xff102030));
            }
        }
        check(timingCallbacks>0&&reentryBlocked);const auto callbackCount=timingCallbacks;
        if(cycle==0){
            mkw_diagnostic_log("[mkw-input-loop] begin 15 seconds with GPU image active; press Cross/Circle and move both sticks\n");
            unsigned connected=0,added=0,removed=0;uint16_t buttons=0;
            int stickMin[4]={127,127,127,127},stickMax[4]={-128,-128,-128,-128};
            for(unsigned poll=0;poll<900;++poll){
                const AuroraEvent* events=aurora_update();check(events!=nullptr);
                for(;events->type!=AURORA_NONE;++events){
                    added+=events->type==AURORA_CONTROLLER_ADDED;
                    removed+=events->type==AURORA_CONTROLLER_REMOVED;
                }
                PADStatus status[PAD_CHANMAX]{};PADRead(status);
                for(const auto& pad:status)if(pad.err==PAD_ERR_NONE){
                    ++connected;buttons|=pad.button;
                    const int values[4]={pad.stickX,pad.stickY,pad.substickX,pad.substickY};
                    for(unsigned a=0;a<4;++a){if(values[a]<stickMin[a])stickMin[a]=values[a];if(values[a]>stickMax[a])stickMax[a]=values[a];}
                }
                sceKernelUsleep(16667);
            }
            char message[384];std::snprintf(message,sizeof(message),"[mkw-input-loop] physical polls=900 connectedSamples=%u buttons=0x%04x added=%u removed=%u axes=[%d..%d,%d..%d,%d..%d,%d..%d]\n",connected,buttons,added,removed,stickMin[0],stickMax[0],stickMin[1],stickMax[1],stickMin[2],stickMax[2],stickMin[3],stickMax[3]);mkw_diagnostic_log(message);
            if(connected&&(buttons&PAD_BUTTON_A)&&(buttons&PAD_BUTTON_B))
                mkw_diagnostic_log("[mkw-input-loop] PASS physical DualSense Cross/Circle through NativePads, SDL and original PADRead\n");
            else mkw_diagnostic_log("[mkw-input-loop] physical Cross/Circle validation incomplete; see samples above\n");
        }
        aurora_shutdown();aurora_shutdown();gpu_wait_slice(0);check(timingCallbacks==callbackCount);check(!current_gx_frame_renderer()&&aurora_get_backend()==BACKEND_NULL);
        rejects([]{aurora_begin_frame();});rejects([]{aurora_update();});check(PADCount()==0);
        char message[140];std::snprintf(message,sizeof(message),"[mkw-bootstrap] PASS cycle %u: resource files, owned config, AGC init, drawn/blank frames, six exact samples and shutdown\n",cycle);mkw_diagnostic_log(message);
        char timingMessage[200];std::snprintf(timingMessage,sizeof(timingMessage),"[mkw-timing] PASS cycle %u: deadline respected, %llu service callbacks, reentry rejected, bounded lock wait and two recorded flips (%.3f ms interval)\n",cycle,(unsigned long long)callbackCount,timing.averageFrameTimeMs);mkw_diagnostic_log(timingMessage);
    }
    // Deliberately reject an unsupported draw, without submitting invalid GPU
    // commands. The failed-frame path must retain the entire runtime and its
    // shaders even if VideoOut itself can be closed successfully.
    config={};config.desiredBackend=BACKEND_AGC;config.resourcesPath="/app0";
    (void)aurora_initialize(0,nullptr,&config);auto* failedOwner=current_gx_frame_renderer();
    auto rejectedState=mkw::test::draw_state(640,528);rejectedState.fog.type=GX_FOG_LIN;
    aurora::gx::register_state()=rejectedState;check(aurora_begin_frame());
    auto rejectedGeometry=mkw::test::draw_geometry(rejectedState);std::vector<uint8_t> rejectedCommand{0x80,0,4};
    rejectedCommand.insert(rejectedCommand.end(),rejectedGeometry.records().begin(),rejectedGeometry.records().end());
    aurora::gx::fifo::write_data(rejectedCommand.data(),uint32_t(rejectedCommand.size()));
    rejects([]{aurora_end_frame();});rejects([]{aurora_shutdown();});
    rejects([]{aurora_wait_for_frame_worker();});rejects([]{aurora_wait_for_frame_worker_for(0);});
    AuroraPresentTiming failedTiming{};aurora_get_present_timing(&failedTiming);check(failedTiming.totalPresentCount==0);
    check(current_gx_frame_renderer()==failedOwner&&aurora_get_backend()==BACKEND_AGC);
    rejects([&]{aurora_initialize(0,nullptr,&config);});
    mkw_diagnostic_log("[mkw-bootstrap] PASS failed-frame shutdown retains runtime/shaders and rejects reinitialization; cleanup deferred to process\n");
    mkw_diagnostic_log("[mkw-bootstrap] PASS invalid config/missing files rollback, duplicate init rejection, two init/shutdown cycles and four retired flips\n");return 0;
}catch(const std::exception& e){mkw_diagnostic_log("[mkw-bootstrap] FAIL ");mkw_diagnostic_log(e.what());mkw_diagnostic_log("\n");return 1;}}
