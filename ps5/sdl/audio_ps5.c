// SPDX-License-Identifier: GPL-3.0-only
// Private SDL3 playback driver using the SharpProspero AudioOut ABI.
#include "SDL_internal.h"
#include "audio/SDL_sysaudio.h"
#include "audio_ps5.h"
extern int sceAudioOutInit(void);
extern int sceAudioOutOpen(int,int,int,unsigned,unsigned,unsigned);
extern int sceAudioOutOutput(int,const void*);
extern int sceAudioOutSetVolume(int,int,const int*);
extern int sceAudioOutClose(int);

struct SDL_PrivateAudioData {
    int port;
    Uint8* buffers;
    unsigned next_buffer;
};
static SDL_AtomicInt opens, closes, blocks, last_error, retained_buffers;
void mkw_ps5_audio_get_stats(MkwPs5AudioStats* out) {
    if (!out) return;
    out->opens=SDL_GetAtomicInt(&opens);out->closes=SDL_GetAtomicInt(&closes);
    out->blocks=SDL_GetAtomicInt(&blocks);out->last_error=SDL_GetAtomicInt(&last_error);
    out->retained_buffers=SDL_GetAtomicInt(&retained_buffers);
}
static bool failure(const char* operation,int rc) {
    SDL_SetAtomicInt(&last_error,rc);
    return SDL_SetError("PS5 AudioOut %s failed: 0x%08x",operation,(unsigned)rc);
}
static bool OpenDevice(SDL_AudioDevice* device) {
    if (device->recording) return SDL_SetError("PS5 audio recording is not implemented");
    device->hidden=SDL_calloc(1,sizeof(*device->hidden));
    if(!device->hidden) return false;
    device->hidden->port=-1;
    // Output hardware is stereo S16 at 48 kHz. SDL's actual AudioStream
    // resampler/mixer converts the runtime's source format before PlayDevice.
    device->spec.format=SDL_AUDIO_S16LE;
    device->spec.channels=2;
    device->spec.freq=48000;
    device->sample_frames=1024;
    SDL_UpdatedAudioDeviceFormat(device);
    device->hidden->buffers=SDL_aligned_alloc(64,(size_t)device->buffer_size*2);
    if(!device->hidden->buffers) return false;
    SDL_memset(device->hidden->buffers,0,(size_t)device->buffer_size*2);
    // SharpProspero AudioOutDevice.OpenStereo tolerates prior initialization.
    // 0xff is SceUser.System, Main is 0, and S16Stereo is 1.
    sceAudioOutInit();
    int port=sceAudioOutOpen(0xff,0,0,1024,48000,1);
    if(port<0) return failure("open",port);
    device->hidden->port=port;
    SDL_AddAtomicInt(&opens,1);
    int volume[8];for(unsigned i=0;i<8;++i)volume[i]=0x8000;
    int rc=sceAudioOutSetVolume(port,3,volume);
    if(rc<0) return failure("volume",rc);
    return true;
}
static Uint8* GetDeviceBuf(SDL_AudioDevice* device,int* size) {
    if(*size<device->buffer_size){SDL_SetError("PS5 audio buffer is smaller than the native grain");return NULL;}
    *size=device->buffer_size;
    Uint8* result=device->hidden->buffers+device->hidden->next_buffer*device->buffer_size;
    device->hidden->next_buffer^=1;
    return result;
}
static bool PlayDevice(SDL_AudioDevice* device,const Uint8* buffer,int bytes) {
    if(bytes!=device->buffer_size) return SDL_SetError("PS5 audio requires a complete native block");
    int rc=sceAudioOutOutput(device->hidden->port,buffer);
    if(rc<0)return failure("output",rc);
    SDL_AddAtomicInt(&blocks,1);
    return true;
}
static bool WaitDevice(SDL_AudioDevice* device) {
    (void)device;
    // Output waits for queue space. Alternating buffers retain the previous
    // queued block until the next call has accepted its successor.
    return true;
}
static void CloseDevice(SDL_AudioDevice* device) {
    struct SDL_PrivateAudioData* hidden=device->hidden;
    if(!hidden)return;
    if(hidden->port>=0){
        int drained=sceAudioOutOutput(hidden->port,NULL);
        int closed=sceAudioOutClose(hidden->port);
        if(closed>=0)SDL_AddAtomicInt(&closes,1);
        if(drained<0||closed<0){
            failure(drained<0?"drain":"close",drained<0?drained:closed);
            SDL_LogError(SDL_LOG_CATEGORY_AUDIO,"%s; retaining native audio buffers",SDL_GetError());
            // SDL closes after joining its producer thread. If native
            // retirement still cannot be established, keep buffers alive.
            SDL_AddAtomicInt(&retained_buffers,1);
            device->hidden=NULL;
            return;
        }
    }
    SDL_aligned_free(hidden->buffers);SDL_free(hidden);device->hidden=NULL;
}
static bool Initialize(SDL_AudioDriverImpl* impl) {
    impl->OpenDevice=OpenDevice;impl->CloseDevice=CloseDevice;
    impl->GetDeviceBuf=GetDeviceBuf;impl->PlayDevice=PlayDevice;impl->WaitDevice=WaitDevice;
    impl->OnlyHasDefaultPlaybackDevice=true;
    return true;
}
AudioBootStrap PRIVATEAUDIO_bootstrap={"ps5","PS5 native AudioOut",Initialize,false,false};
