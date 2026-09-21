// SPDX-License-Identifier: GPL-3.0-only
#include "audio_backend.h"
#include "audio_ps5.h"
#include <SDL3/SDL.h>
#include <array>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <cstring>
extern "C" void mkw_diagnostic_log(const char*);
extern "C" int mkw_test_sdl_audio(){try{
    mkw_diagnostic_log("[mkw-audio] begin real AudioBackend -> SDL -> PS5 AudioOut, quiet stereo tone\n");
    auto& backend=AudioBackend::Instance();
    struct Stop {~Stop(){AudioBackend::Instance().Shutdown();}} stop;
    backend.SetMasterVolume(0.05f);
    if(!backend.Init(32000,2))throw std::runtime_error(SDL_GetError());
    if(std::strcmp(SDL_GetCurrentAudioDriver(),"ps5"))throw std::runtime_error("Wrong audio driver");
    std::array<uint8_t,1280> block{};
    for(unsigned part=0;part<200;++part){
        const double envelope=part<20?part/20.0:(part>=180?(199-part)/20.0:1.0);
        for(unsigned frame=0;frame<320;++frame){
            const double time=(part*320+frame)/32000.0;
            const auto left=static_cast<int16_t>(26000*envelope*std::sin(time*440*6.283185307179586));
            const auto right=static_cast<int16_t>(26000*envelope*std::sin(time*660*6.283185307179586));
            block[4*frame]=uint16_t(right)>>8;block[4*frame+1]=uint16_t(right)&255;
            block[4*frame+2]=uint16_t(left)>>8;block[4*frame+3]=uint16_t(left)&255;
        }
        if(!backend.PushWiiAiSamplesBE16(block.data(),block.size()))throw std::runtime_error(SDL_GetError());
        SDL_Delay(10);
    }
    SDL_Delay(180);backend.Shutdown();
    MkwPs5AudioStats stats{};mkw_ps5_audio_get_stats(&stats);
    char log[200];std::snprintf(log,sizeof(log),"[mkw-audio] native opens=%d closes=%d blocks=%d last-error=0x%08x retained=%d\n",stats.opens,stats.closes,stats.blocks,unsigned(stats.last_error),stats.retained_buffers);mkw_diagnostic_log(log);
    if(stats.opens!=1||stats.closes!=1||stats.blocks<40||stats.last_error||stats.retained_buffers)throw std::runtime_error("Native playback/retirement counters failed");
    if(SDL_WasInit(SDL_INIT_AUDIO))throw std::runtime_error("Audio subsystem reference retained");
    if(!backend.Init(48000,2))throw std::runtime_error(SDL_GetError());
    std::array<int16_t,960> silence{};
    for(unsigned i=0;i<10;++i){if(!backend.PushSamplesLE16(silence.data(),silence.size()))throw std::runtime_error(SDL_GetError());SDL_Delay(10);}
    SDL_Delay(180);backend.Shutdown();mkw_ps5_audio_get_stats(&stats);
    if(stats.opens!=2||stats.closes!=2||stats.retained_buffers||SDL_WasInit(SDL_INIT_AUDIO))throw std::runtime_error("Native reopen/shutdown failed");
    mkw_diagnostic_log("[mkw-audio] PASS two native lifecycles and queued output; audible result requires listener confirmation\n");
    return 0;
}catch(const std::exception& e){mkw_diagnostic_log("[mkw-audio] FAIL ");mkw_diagnostic_log(e.what());mkw_diagnostic_log("\n");return 1;}}
