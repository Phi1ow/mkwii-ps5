// SPDX-License-Identifier: GPL-3.0-only
#include "gx_perf_stats.h"
#include "async_log.h"
#include "gpu_wait_service.h"
#include <cstdio>
#include "gx_frame_renderer.h"
#include "gx_frame_timing.h"
#include "gpu_wait_service.h"
#include "aurora_input.h"
#include <SDL3/SDL_error.h>
#include <aurora/gfx.h>
#include "gpu_shader.h"
#include "gx/fifo.hpp"
#include "gx/register_backend.hpp"
#include <array>
#include <cstdio>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>
extern "C" int sceAgcInit(void*,unsigned);
namespace aurora {AuroraConfig g_config{};}
namespace {
using namespace mkw::agc;
std::vector<uint8_t> read_shader(const std::string& path){
    std::unique_ptr<FILE,decltype(&std::fclose)> file(std::fopen(path.c_str(),"rb"),std::fclose);
    if(!file)throw std::runtime_error("Cannot open AGC shader: "+path);
    if(std::fseek(file.get(),0,SEEK_END))throw std::runtime_error("Cannot seek AGC shader: "+path);
    const auto length=std::ftell(file.get());
    if(length<64||length>1024*1024||std::fseek(file.get(),0,SEEK_SET))throw std::runtime_error("Invalid AGC shader file size: "+path);
    std::vector<uint8_t> result(static_cast<size_t>(length));
    if(std::fread(result.data(),1,result.size(),file.get())!=result.size())throw std::runtime_error("Short AGC shader read: "+path);
    return result;
}
struct Runtime {
    std::string name,user,cache,resources,pipeline;
    AuroraConfig config;
    std::array<std::unique_ptr<GpuShader>,4> shaders;
    std::unique_ptr<GxFrameRenderer> frames;
    mkw::input::AuroraInput input;
    explicit Runtime(const AuroraConfig& c):name(c.appName?c.appName:"Mario Kart Wii"),
        user(c.userPath?c.userPath:"/data/wiicompiled"),cache(c.cachePath?c.cachePath:user+"/Cache"),
        resources(c.resourcesPath?c.resourcesPath:"/app0"),pipeline(c.pipelineCachePath?c.pipelineCachePath:cache),config(c){
        config.appName=name.c_str();config.userPath=user.c_str();config.cachePath=cache.c_str();
        config.resourcesPath=resources.c_str();config.pipelineCachePath=pipeline.c_str();
        config.desiredBackend=BACKEND_AGC;config.msaa=1;config.maxTextureAnisotropy=c.maxTextureAnisotropy?c.maxTextureAnisotropy:16;
        std::array<std::vector<uint8_t>,4> binaries;
        constexpr std::array<const char*,4> names{"gx_vertex.sb","gx_pixel.sb","copy_vertex.sb","copy_pixel.sb"};
        for(unsigned i=0;i<4;++i){binaries[i]=read_shader(resources+"/shaders/"+names[i]);(void)parse_shader_binary(binaries[i]);}
        static uint64_t agcState=0;static bool agcInitialized=false;
        if(!agcInitialized){if(sceAgcInit(&agcState,8)<0)throw std::runtime_error("AGC initialization failed");agcInitialized=true;}
        for(unsigned i=0;i<4;++i)shaders[i]=std::make_unique<GpuShader>(binaries[i]);
        frames=std::make_unique<GxFrameRenderer>(GxPreparedShaders{shaders[0]->handle(),shaders[1]->handle(),shaders[2]->handle(),shaders[3]->handle()},config.maxTextureAnisotropy);
        frames->set_disable_copy_filter(config.disableCopyFilter);
    }
    ~Runtime(){
        // Failed frames or VideoOut close leave GPU owners alive. Retain
        // shader storage as well until the process releases its resources.
        if(frames&&frames->close()<0){(void)frames.release();for(auto& s:shaders)(void)s.release();}
    }
};
std::unique_ptr<Runtime> runtime;
void validate(const AuroraConfig& c){
    if(c.desiredBackend!=BACKEND_AUTO&&c.desiredBackend!=BACKEND_AGC)throw std::invalid_argument("PS5 requires the AGC backend");
    const auto a=c.maxTextureAnisotropy;
    if((a&&a!=1&&a!=2&&a!=4&&a!=8&&a!=16)||c.msaa>1)throw std::invalid_argument("Unsupported AGC sample/anisotropy configuration");
    if(c.allowTextureReplacements||c.allowTextureDumps||c.imGuiInitCallback||c.mem1Size||c.mem2Size)
        throw std::invalid_argument("Optional Aurora texture/UI/memory services are not implemented by the PS5 bootstrap");
}
}
// Diagnostic, inert unless /app0/UserData/microbench.txt exists (perf_microbench_ps5.cpp).
extern "C" void mkw_run_perf_microbench_if_requested();
// Diagnostic, inert unless /app0/UserData/profile.txt exists (guest_sampler_ps5.cpp).
extern "C" void mkw_start_guest_sampler_if_requested();
// thread_placement_ps5.cpp: report always; confine only with UserData/placement.txt.
extern "C" void mkw_report_thread_placement(const char* who);
extern "C" void mkw_apply_thread_placement_if_requested();
extern "C" AuroraInfo aurora_initialize(int,char**,const AuroraConfig* config){
    require_outside_gpu_wait_callback();
#if !defined(MKW_PS5_RELEASE)  // player packages skip the flag-file diagnostics
    mkw_run_perf_microbench_if_requested();
    mkw_start_guest_sampler_if_requested();
#endif
    std::lock_guard lock(aurora::renderer_gpu_mutex());
    if(!config)throw std::invalid_argument("Missing Aurora configuration");
    if(runtime||current_gx_frame_renderer())throw std::logic_error("Aurora already owns an active renderer");
    validate(*config);
    if(aurora::gx::fifo::get_buffer_size())throw std::logic_error("GX commands queued before initialization");
    auto candidate=std::make_unique<Runtime>(*config);
    aurora::gx::fifo::init();
    runtime=std::move(candidate);aurora::g_config=runtime->config;
    reset_gx_frame_timing();set_gpu_wait_callback(nullptr);
    exchange_gx_frame_renderer(runtime->frames.get());
    auto plan=snapshot_gx_video();
    AuroraInfo info{};info.backend=BACKEND_AGC;info.userPath=runtime->user.c_str();info.cachePath=runtime->cache.c_str();
    auto& s=info.windowSize;s.width=s.native_fb_width=1920;s.height=s.native_fb_height=1080;
    s.fb_width=plan.extent.targetWidth;s.fb_height=plan.extent.targetHeight;s.scale=1;
    return info;
}
extern "C" void aurora_shutdown(){
    require_outside_gpu_wait_callback();
    std::lock_guard lock(aurora::renderer_gpu_mutex());if(!runtime)return;
    if(runtime->input.close()<0)throw std::runtime_error("Aurora input shutdown could not close native handles");
    if(runtime->frames->close()<0)throw std::runtime_error("Aurora shutdown cannot prove GPU retirement; resources retained");
    exchange_gx_frame_renderer(nullptr);set_gpu_wait_callback(nullptr);runtime.reset();aurora::g_config={};
}
extern "C" const AuroraEvent* aurora_update(){
    require_outside_gpu_wait_callback();
    // Like upstream SDL event dispatch, update and shutdown belong to the
    // producer thread. Do not hold the renderer mutex during input callbacks.
    if(!runtime)throw std::logic_error("Aurora update before initialization");
    if(!runtime->input.initialized()){
        if(!runtime->input.initialize(runtime->config.allowJoystickBackgroundEvents))throw std::runtime_error(SDL_GetError());
#if !defined(MKW_PS5_RELEASE)
        // First poll: the guest thread and the runtime's service threads now exist.
        mkw_report_thread_placement("guest thread");
        mkw_apply_thread_placement_if_requested();
#endif
    }
    mkw::agc::HostPhaseScope phase(mkw::agc::HostPhase::InputPoll);
    const auto started=mkw::agc::gpu_clock_nanos();
    const auto* events=runtime->input.poll();
    if(const auto elapsed=mkw::agc::gpu_clock_nanos()-started;elapsed>500'000'000u)
        mkw_log("[mkw-stall] %.0f ms in aurora_update input poll\n",double(elapsed)*1e-6);
    return events;
}
extern "C" AuroraBackend aurora_get_backend(){std::lock_guard lock(aurora::renderer_gpu_mutex());return runtime?BACKEND_AGC:BACKEND_NULL;}
extern "C" const AuroraBackend* aurora_get_available_backends(size_t* count){static const AuroraBackend backends[]{BACKEND_AGC};if(count)*count=1;return backends;}
extern "C" void aurora_set_disable_copy_filter(bool value){std::lock_guard lock(aurora::renderer_gpu_mutex());aurora::g_config.disableCopyFilter=value;if(runtime)runtime->frames->set_disable_copy_filter(value);}
