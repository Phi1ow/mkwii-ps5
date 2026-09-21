// SPDX-License-Identifier: GPL-3.0-only
#include "texture_layout.h"
#include "gpu_fence.h"
#include <cstring>
#include <exception>
#include <stdexcept>
extern "C" void mkw_diagnostic_log(const char*);
extern "C" int mkw_gpu_completion_packet(unsigned long long address,unsigned long long serial,unsigned int* words) {
    try {auto packet=mkw::agc::encode_gpu_completion(address,serial);std::memcpy(words,packet.data(),sizeof(packet));return 0;}
    catch(const std::exception& e){mkw_diagnostic_log(e.what());return 1;}
}
#ifdef MKW_OWNED_COLOR_TARGETS
#include "color_target.h"
#include <memory>
#include <cstdio>
extern "C" int sceVideoOutClose(int);
// Kept alive on every error path: the app parks until the external launcher
// closes it. Destruction after a successful test requires explicit detachment.
static std::shared_ptr<mkw::agc::GpuColorTarget> targets[3];
#ifdef MKW_GPU_BLIT
#include "agc_blit.h"
static std::unique_ptr<mkw::agc::AgcBlit> blitter;
static std::shared_ptr<mkw::agc::GpuColorTarget> cropped;
#ifdef MKW_DEPTH_TEST
void mkw_test_depth_target(mkw::agc::AgcBlit&);
void mkw_check_gx_depth_clear();
#endif
#ifdef MKW_GX_COPY_EXECUTE
#include "gx_copy_texture_cache.h"
static mkw::agc::ColorCopyHandle executedCopy;
mkw::agc::ColorCopyHandle mkw_execute_gx_texture_copy(mkw::agc::AgcBlit&,std::shared_ptr<mkw::agc::GpuColorTarget>);
#endif
#endif
#ifdef MKW_GX_COPY_BINDING
std::array<uint32_t,8> mkw_bind_color_copy(std::shared_ptr<const mkw::agc::GpuColorTarget>,unsigned copyFormat=6,unsigned sampleFormat=6);
void mkw_release_color_copy();
#endif
template<class F> static int checked(F&& operation) {
    try { operation(); return 0; }
    catch(const std::exception& e) {
        mkw_diagnostic_log("[mkw-color-target] FAIL ");
        mkw_diagnostic_log(e.what());mkw_diagnostic_log("\n");return 1;
    }
}
#endif
extern "C" int mkw_framebuffer_view(void* address,unsigned long bytes,unsigned int* descriptor){
    try {
        const mkw::agc::TextureLayout layout(1920,1080,1);
        if(!address||bytes<layout.byte_size())throw std::invalid_argument("Truncated color target");
#ifdef MKW_OWNED_COLOR_TARGETS
        if(!targets[0]||targets[0]->data()!=address)throw std::logic_error("Wrong source target owner");
#ifdef MKW_GX_COPY_BINDING
#ifdef MKW_GPU_BLIT
        if(!cropped)throw std::logic_error("Blit result missing");
        auto words=mkw_bind_color_copy(cropped,
#ifdef MKW_GX_COPY_READBACK
            0x23,3
#else
            6,6
#endif
        );
#else
        auto words=mkw_bind_color_copy(targets[0]);
#endif
#else
        auto words=targets[0]->texture_descriptor();
#endif
#else
        auto words=layout.bgra_descriptor(reinterpret_cast<uintptr_t>(address));
#endif
        std::memcpy(descriptor,words.data(),sizeof(words));
        return 0;
    }catch(const std::exception& e){mkw_diagnostic_log("[mkw-framebuffer] FAIL ");mkw_diagnostic_log(e.what());mkw_diagnostic_log("\n");return 1;}
}
#ifdef MKW_OWNED_COLOR_TARGETS
#ifdef MKW_GPU_BLIT
extern "C" int mkw_framebuffer_blit(void* vs,void* ps) {
    return checked([&]{
#ifdef MKW_INSPECT_DEPTH_DEFAULTS
        extern void mkw_inspect_depth_defaults();mkw_inspect_depth_defaults();
#endif
        blitter=std::make_unique<mkw::agc::AgcBlit>(vs,ps,
#ifdef MKW_GX_FILTERED_COPY
            mkw::agc::AgcBlit::ShaderAbi::GxCopy
#else
            mkw::agc::AgcBlit::ShaderAbi::SdkTexture
#endif
        );
#ifdef MKW_GX_COPY_EXECUTE
        executedCopy=mkw_execute_gx_texture_copy(*blitter,targets[0]);
        // Only this diagnostic keeps a mutable alias for explicit final release.
        cropped=std::const_pointer_cast<mkw::agc::GpuColorTarget>(executedCopy->target);
#else
        cropped=std::make_shared<mkw::agc::GpuColorTarget>(
#ifdef MKW_GX_COPY_READBACK
            640,360
#else
            960,540
#endif
        );
        cropped->clear_after_gpu_idle(0xff123456);
#ifdef MKW_GX_FILTERED_COPY
#ifdef MKW_GX_COPY_READBACK
        mkw::agc::GxCopyOptions options;options.format=0x23;
        if(blitter->copy(targets[0],cropped,{240,135,1440,810},{0,0,640,360},&options)!=1)
            throw std::logic_error("Typed copy completion differs");
        mkw_diagnostic_log("[mkw-gx-copy] RA8 copy retired at native 640x360 dimensions\n");
#elif defined(MKW_GX_COPY_FORMATS)
        static constexpr unsigned formats[16]={0,1,2,3,4,5,6,0x20,0x22,0x23,0x27,0x28,0x29,0x2a,0x2b,0x2c};
        for(unsigned i=0;i<16;++i){
            mkw::agc::GxCopyOptions options;options.format=formats[i];
            auto serial=blitter->copy(targets[0],cropped,{240,135,1440,810},{(i%4)*240,(i/4)*135,240,135},&options);
            if(serial!=i+1)throw std::logic_error("Color-format copy serial differs");
        }
        mkw_diagnostic_log("[mkw-gx-copy] 16 color format copies retired into a 4x4 target, source RGBA 64/128/192/80\n");
#else
        mkw::agc::GxCopyOptions options;options.filter={16,32,16};options.rowStride=100;
        auto serial=blitter->copy(targets[0],cropped,{240,500,720,1},{0,0,960,270},&options);
        if(serial!=1)throw std::logic_error("First filtered-copy serial differs");
        options.format=3;options.forceOpaqueAlpha=true;
        serial=blitter->copy(targets[0],cropped,{240,550,720,1},{0,270,960,270},&options);
        if(serial!=2)throw std::logic_error("Second filtered-copy serial differs");
        mkw_diagnostic_log("[mkw-gx-copy] two filtered copies retired, weights 16/32/16, row stride 100, RGBA8 upper and IA8 lower\n");
#endif
#else
        auto serial=blitter->copy(targets[0],cropped,{0,0,1920,1080},{0,0,960,540});
        if(serial!=1)throw std::logic_error("First blit serial differs");
        // Overwrite with the left half of the source quad: output must contain
        // only red above blue, never the green/white of the original texture.
        serial=blitter->copy(targets[0],cropped,{240,135,720,810},{0,0,960,540});
        if(serial!=2)throw std::logic_error("Reused blit serial differs");
        mkw_diagnostic_log("[mkw-blit] two copies retired: full downscale then left crop 720x810 -> 960x540; serials 1,2; no intermediate pixel readback\n");
#endif
#endif
#ifdef MKW_DEPTH_TEST
        mkw_test_depth_target(*blitter);
#endif
    });
}
#ifdef MKW_INSPECT_DEPTH_DEFAULTS
extern "C" void* sceAgcGetRegisterDefaults();
void mkw_inspect_depth_defaults(){
    struct Register{uint16_t offset,pad;uint32_t value;};
    auto* descriptor=static_cast<unsigned char*>(sceAgcGetRegisterDefaults());
    if(!descriptor)throw std::runtime_error("Missing AGC defaults descriptor");
    Register** blocks;uint32_t count;
    std::memcpy(&blocks,descriptor,sizeof(blocks));std::memcpy(&count,descriptor+0x20,sizeof(count));
    if(!blocks||!blocks[0]||!count||count>3000)throw std::runtime_error("Invalid AGC defaults range");
    char line[100];std::snprintf(line,sizeof(line),"[mkw-depth-defaults] context record count=%u\n",count);mkw_diagnostic_log(line);
    for(uint32_t i=0;i<count;++i){const auto& r=blocks[0][i];
        std::snprintf(line,sizeof(line),"[mkw-depth-defaults] %04u %04x %08x\n",i,r.offset,r.value);mkw_diagnostic_log(line);}
}
#endif
extern "C" int mkw_framebuffer_blit_inspect() {
    return checked([&]{
    // Failure isolation only, after the final display draw has retired.
    // This readback never makes data visible to an intervening GPU draw.
    char message[180];
#ifdef MKW_GX_COPY_EXECUTE
    const auto& sourceLayout=targets[0]->layout();
    auto* sourceBytes=static_cast<const unsigned char*>(targets[0]->data());
    for(size_t offset=0;offset<sourceLayout.byte_size();offset+=64)__builtin_ia32_clflush(sourceBytes+offset);
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    for(unsigned y=0;y<1080;++y)for(unsigned x=0;x<1920;++x){
        unsigned expected=0x50201828;
        if(x>=240&&x<1680&&y>=135&&y<945){
            expected=0x40102030;
            if(x<960&&y<540)expected=0x40607080;
            if(x>=960&&y>=540)expected=0xa0102030;
        }
        unsigned actual;std::memcpy(&actual,sourceBytes+sourceLayout.pixel_offset(x,y,0),4);
        if(actual!=expected){
            std::snprintf(message,sizeof(message),"[mkw-gx-clear] FAIL pixel %u,%u: %08x expected %08x\n",x,y,actual,expected);
            mkw_diagnostic_log(message);throw std::runtime_error("GPU EFB clear pixel differs");
        }
    }
    mkw_diagnostic_log("[mkw-gx-clear] PASS 2073600 exact EFB pixels: alpha before copy, RGBA rectangle after copy, RGB-only and alpha-only masks, outside RGB preserved\n");
#ifdef MKW_DEPTH_TEST
    mkw_check_gx_depth_clear();
#endif
#endif
    for(unsigned y:{270u,810u})for(unsigned x:{480u,1440u}){
        auto* pixel=reinterpret_cast<volatile unsigned int*>(static_cast<char*>(targets[0]->data())+targets[0]->layout().pixel_offset(x,y,0));
        __builtin_ia32_clflush(const_cast<const unsigned int*>(pixel));__atomic_thread_fence(__ATOMIC_SEQ_CST);
        std::snprintf(message,sizeof(message),"[mkw-blit-inspect] source %u,%u = %08x\n",x,y,*pixel);mkw_diagnostic_log(message);
    }
    unsigned width=cropped->layout().width(),height=cropped->layout().height();
    for(unsigned y:{height/4,height*3/4})for(unsigned x:{width/4,width*3/4}){
        auto* pixel=reinterpret_cast<volatile unsigned int*>(static_cast<char*>(cropped->data())+cropped->layout().pixel_offset(x,y,0));
        __builtin_ia32_clflush(const_cast<const unsigned int*>(pixel));__atomic_thread_fence(__ATOMIC_SEQ_CST);
        std::snprintf(message,sizeof(message),"[mkw-blit-inspect] cropped %u,%u = %08x\n",x,y,*pixel);mkw_diagnostic_log(message);
    }
#ifdef MKW_GX_COPY_READBACK
    extern int mkw_check_color_copy_bytes();
    if(mkw_check_color_copy_bytes())throw std::runtime_error("Typed copy RAM encoding failed");
#endif
    });
}
#endif
extern "C" int mkw_color_target_create(unsigned int slot,void** data,unsigned long* size) {
    return checked([&]{
        if(slot>=3||targets[slot]||!data||!size)throw std::invalid_argument("Invalid target allocation request");
        targets[slot]=std::make_shared<mkw::agc::GpuColorTarget>(1920,1080);
        *data=targets[slot]->data();*size=targets[slot]->allocation_size();
        mkw_diagnostic_log("[mkw-color-target] allocated owned BGRA8 1920x1080 target\n");
    });
}
extern "C" int mkw_color_target_clear(unsigned int slot,unsigned int color) {
    return checked([&]{
        if(slot>=2||!targets[slot])throw std::invalid_argument("Missing target for idle clear");
        targets[slot]->clear_after_gpu_idle(color);
    });
}
extern "C" int mkw_color_target_context(unsigned int slot,void* context) {
    return checked([&]{
        struct Register {uint16_t offset,pad;uint32_t value;};
        static_assert(sizeof(Register)==8);
        if(slot>=2||!targets[slot]||!context)throw std::invalid_argument("Missing render target context");
        auto* registers=static_cast<Register*>(context);
        std::array<uint32_t,16> defaults;
        for(size_t i=0;i<defaults.size();++i) {
            if(registers[i].offset!=mkw::agc::ColorTargetOffsets[i])throw std::invalid_argument("Unexpected target register order");
            defaults[i]=registers[i].value;
        }
        auto values=targets[slot]->context_values(defaults);
        for(size_t i=0;i<values.size();++i)registers[i].value=values[i];
        mkw_diagnostic_log("[mkw-color-target] production AGC context encoded\n");
    });
}
extern "C" int mkw_color_targets_release(int handle) {
    // Called only after every draw has retired (flip or completion label),
    // with the final display flip retired and pending==0.
    // Unregistering the set while its last buffer is still displayed returned
    // 0x80290009 on PPSA99545 despite pending==0. Close the output before
    // releasing any mapped storage; a failed close retains both owners.
    int r=sceVideoOutClose(handle);
    char message[160];
    std::snprintf(message,sizeof(message),"[mkw-color-target] video close=0x%08x\n",unsigned(r));
    mkw_diagnostic_log(message);
    if(r)return 1;
#ifdef MKW_GX_COPY_BINDING
    mkw_release_color_copy();
#endif
#ifdef MKW_GPU_BLIT
#ifdef MKW_GX_COPY_EXECUTE
    if(!executedCopy||executedCopy.use_count()!=1||executedCopy->nativeReadback.use_count()!=1)return 1;
    auto native=std::const_pointer_cast<mkw::agc::GpuColorTarget>(executedCopy->nativeReadback);
    if(native->release_after_gpu_idle())return 1;
    executedCopy.reset();native.reset();
    mkw_diagnostic_log("[mkw-gx-copy-execute] native readback target released after last memory/material owner\n");
#endif
    if(cropped&&cropped->release_after_gpu_idle())return 1;
    cropped.reset();blitter.reset();
    mkw_diagnostic_log("[mkw-blit] retired cropped target and reusable command arena released\n");
#endif
    for(auto& target:targets) {
        if(!target)continue;
        if(target->release_after_gpu_idle())return 1;
        target.reset();
        mkw_diagnostic_log("[mkw-color-target] idle detached target unmapped and released\n");
    }
    return 0;
}
#endif
