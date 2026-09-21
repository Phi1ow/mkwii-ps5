// SPDX-License-Identifier: GPL-3.0-only
// Real WiiCompiled AudioBackend + real SDL resampler + PS5 driver; only
// the device's native calls are simulated. No Windows sound is emitted.
#include "audio_backend.h"
#include "audio_ps5.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>
namespace {
std::mutex mutex;
std::vector<int16_t> played;
std::array<int16_t,2048> pendingCopy;
const int16_t* pending=nullptr;
bool portOpen=false, badAbi=false, overwritten=false;
std::atomic<bool> failOpen{false};
unsigned checks=0;
void check(bool value,const char* label){++checks;if(!value)throw std::runtime_error(label);}
void consume(){if(pending){
    overwritten|=std::memcmp(pending,pendingCopy.data(),sizeof(pendingCopy))!=0;
    played.insert(played.end(),pendingCopy.begin(),pendingCopy.end());pending=nullptr;
}}
void clearPlayed(){std::lock_guard lock(mutex);played.clear();}
unsigned countPair(int left,int right){std::lock_guard lock(mutex);unsigned count=0;
    for(size_t i=0;i+1<played.size();i+=2)if(played[i]==left&&played[i+1]==right)++count;return count;}
void feedWii(){
    std::array<uint8_t,1280> input{};
    for(size_t i=0;i<input.size();i+=4){input[i]=0xf8;input[i+1]=0x30;input[i+2]=0x03;input[i+3]=0xe8;}
    for(unsigned i=0;i<25;++i){check(AudioBackend::Instance().PushWiiAiSamplesBE16(input.data(),input.size()),"Wii block rejected");std::this_thread::sleep_for(std::chrono::milliseconds(10));}
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
}
}
extern "C" int sceAudioOutInit(){return 0;}
extern "C" int sceAudioOutOpen(int user,int type,int index,unsigned grain,unsigned frequency,unsigned format){
    std::lock_guard lock(mutex);
    badAbi|=user!=0xff||type!=0||index!=0||grain!=1024||frequency!=48000||format!=1||portOpen;
    if(failOpen.load())return -123;
    portOpen=true;return 7;
}
extern "C" int sceAudioOutSetVolume(int port,int flags,const int* levels){
    std::lock_guard lock(mutex);badAbi|=port!=7||flags!=3;
    for(unsigned i=0;i<8;++i)badAbi|=levels[i]!=0x8000;return 0;
}
extern "C" int sceAudioOutOutput(int port,const void* data){
    std::this_thread::sleep_for(std::chrono::microseconds(21333));
    std::lock_guard lock(mutex);badAbi|=port!=7||!portOpen;
    consume();
    if(data){badAbi|=(reinterpret_cast<uintptr_t>(data)%64)!=0;pending=static_cast<const int16_t*>(data);std::memcpy(pendingCopy.data(),data,sizeof(pendingCopy));}
    return 0;
}
extern "C" int sceAudioOutClose(int port){std::lock_guard lock(mutex);badAbi|=port!=7||!portOpen||pending;portOpen=false;return 0;}

int main(){try{
    SDL_SetMainReady();SDL_SetHint(SDL_HINT_AUDIO_DRIVER,"ps5");
    auto& backend=AudioBackend::Instance();
    check(backend.Init(32000,2),"32 kHz backend init failed");
    check(std::strcmp(SDL_GetCurrentAudioDriver(),"ps5")==0,"wrong SDL driver");
    feedWii();check(countPair(1000,-2000)>2048,"BE right/left conversion or resampled DC failed");
    backend.SetMasterVolume(0.5f);clearPlayed();feedWii();
    check(countPair(500,-1000)>2048,"software volume not applied");
    backend.SetMuted(true);std::this_thread::sleep_for(std::chrono::milliseconds(100));clearPlayed();feedWii();
    check(countPair(0,0)>4096,"mute not applied");check(countPair(500,-1000)==0,"nonzero muted samples");
    backend.Shutdown();check(SDL_WasInit(SDL_INIT_AUDIO)==0,"audio subsystem reference leaked");
    check(!badAbi&&!overwritten&&!portOpen&&!pending,"native ABI or queued buffer lifetime violated");
    MkwPs5AudioStats stats{};mkw_ps5_audio_get_stats(&stats);
    check(stats.opens==1&&stats.closes==1&&stats.blocks>=30&&stats.retained_buffers==0,"native lifecycle counters wrong");

    failOpen=true;check(!backend.Init(32000,2),"failed native open accepted");
    backend.Shutdown();check(SDL_WasInit(SDL_INIT_AUDIO)==0,"failed initialization leaked SDL audio ownership");
    failOpen=false;backend.SetMuted(false);backend.SetMasterVolume(1.0f);
    check(backend.Init(32000,2),"retry after failed open failed");
    check(backend.Init(48000,2),"source rate change failed");
    std::array<int16_t,960> samples;for(size_t i=0;i<samples.size();i+=2){samples[i]=-3000;samples[i+1]=4000;}
    clearPlayed();
    for(unsigned i=0;i<25;++i){check(backend.PushSamplesLE16(samples.data(),samples.size()),"LE block rejected");std::this_thread::sleep_for(std::chrono::milliseconds(10));}
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    check(countPair(-3000,4000)>2048,"native-rate LE stream altered");
    backend.Shutdown();check(SDL_WasInit(SDL_INIT_AUDIO)==0,"rate change leaked SDL audio reference");
    check(!badAbi&&!overwritten&&!portOpen&&!pending,"reopened native port lifetime violated");
    mkw_ps5_audio_get_stats(&stats);check(stats.opens==stats.closes&&stats.retained_buffers==0,"unbalanced native ports");
    SDL_Quit();std::printf("PASS actual AudioBackend/SDL/native-driver contract: %u checks, %d blocks; hardware calls simulated\n",checks,stats.blocks);return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL audio contract check %u: %s; SDL=%s\n",checks,e.what(),SDL_GetError());AudioBackend::Instance().Shutdown();SDL_Quit();return 1;}}
