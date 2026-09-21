// SPDX-License-Identifier: GPL-3.0-only
// Native AGC ABI and resource descriptors from ps5link-sdk/examples/gpu_cube.
#include "agc_blit.h"
#include "gpu_wait_service.h"
#include "gx_perf_stats.h"
#include "gpu_fence.h"
#include "gx_copy_color.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <stdexcept>
#include <vector>
extern "C" {
uint64_t sceKernelGetDirectMemorySize();
int sceKernelAllocateDirectMemory(int64_t,int64_t,size_t,size_t,int,int64_t*);
int sceKernelMapDirectMemory(void**,size_t,int,int,int64_t,size_t);
int sceKernelReleaseDirectMemory(int64_t,size_t);
int sceKernelMunmap(void*,size_t);
int sceKernelUsleep(unsigned);
void* sceAgcGetRegisterDefaults();
int sceAgcLinkShaders(void*,void*,void*,void*,void*,unsigned);
void sceAgcDcbSetCxRegistersIndirect(void*,void*,unsigned);
void sceAgcDcbSetShRegistersIndirect(void*,void*,unsigned);
void sceAgcDcbSetUcRegistersIndirect(void*,void*,unsigned);
void sceAgcCbSetShRegisterRangeDirect(void*,unsigned,unsigned*,unsigned);
void* sceAgcDcbSetIndexSize(void*,unsigned char,unsigned char);
void* sceAgcDcbSetIndexBuffer(void*,void*);
void* sceAgcDcbSetIndexCount(void*,unsigned);
void* sceAgcDcbDrawIndex(void*,unsigned,void*,uint64_t);
int sceAgcDriverSubmitDcb(void*);
int sceAgcSuspendPoint();
}
namespace mkw::agc {
namespace {
struct Register {uint16_t offset,pad;uint32_t value;};
struct Dcb {uint32_t *bottom,*top,*up,*down;intptr_t callback;void* user;uint32_t reserved,pad;};
static_assert(sizeof(Register)==8&&sizeof(Dcb)==56);
unsigned char exhausted(Dcb* d,unsigned,void*){*static_cast<bool*>(d->user)=true;return 0;}
template<class T> T field(void* p,size_t offset){T value;std::memcpy(&value,static_cast<char*>(p)+offset,sizeof(T));return value;}
unsigned resource(void* shader,unsigned kind,unsigned slot,unsigned size) {
    auto* ud=field<void*>(shader,8);
    if(!ud)throw std::invalid_argument("Blit shader has no user data");
    auto count=field<uint16_t>(ud,46+2*kind);
    auto* sharps=field<uint16_t*>(ud,8+sizeof(void*)*kind);
    if(!sharps||slot>=count)throw std::invalid_argument("Blit shader resource missing");
    unsigned offset=sharps[slot]&0x7fff;
    if(offset+size>16)throw std::invalid_argument("Blit shader resource exceeds user registers");
    return offset;
}
}
struct AgcBlit::Impl {
    static constexpr size_t Bytes=131072,Alignment=65536;
    // Pause-instruction polls per spin attempt; the label is re-checked every 64.
    static constexpr unsigned kSpinPollsPerAttempt=8192;
    // A submitted blit keeps its own arena (commands, registers, geometry,
    // constants, completion label) and the owners the GPU reads through until
    // its label lands. Up to InFlight blits run while the caller continues.
    static constexpr unsigned InFlight=16;
    struct Arena {
        void* memory=nullptr;int64_t physical=-1;
        uint64_t serial=0;bool submitted=false;
        std::shared_ptr<const GpuColorTarget> source;
        std::shared_ptr<GpuColorTarget> target;
        std::shared_ptr<GpuDepthTarget> depthTarget;
        std::shared_ptr<const GpuDepthTarget> depthSource;
        uint64_t* label() const {return reinterpret_cast<uint64_t*>(static_cast<char*>(memory)+114688);}
        bool landed() const {
            __builtin_ia32_clflush(label());__atomic_thread_fence(__ATOMIC_SEQ_CST);
            return *static_cast<volatile uint64_t*>(label())==serial;
        }
        void release(){submitted=false;source.reset();target.reset();depthTarget.reset();depthSource.reset();}
    };
    std::array<Arena,InFlight> arenas;
    Arena* current=nullptr;
    void *vs,*ps;unsigned cbSlot,vbSlot,texSlot,samplerSlot;
    ShaderAbi abi;
    uint64_t serial=0;
    // A failed submission or a completion timeout keeps every owner and refuses further work.
    bool failed=false;
    // 1x1 fill colour for clears; reused once the previous blit released it.
    std::shared_ptr<GpuColorTarget> constant;
    std::shared_ptr<GpuColorTarget> fill_constant(uint32_t bgra){
        if(!constant||constant.use_count()!=1)constant=std::make_shared<GpuColorTarget>(1,1);
        constant->clear_after_gpu_idle(bgra);
        return constant;
    }
    // Cached once: the linkage is a pure function of the two prepared shaders
    // and the driver register defaults are immutable for the process.
    Register linkage[34]{};Register primitive[3]{};
    std::vector<Register> defaults;
    Impl(void* v,void* p,ShaderAbi shaderAbi):vs(v),ps(p),abi(shaderAbi) {
        if(!vs||!ps)throw std::invalid_argument("Missing prepared blit shaders");
        if(abi!=ShaderAbi::SdkTexture&&abi!=ShaderAbi::GxCopy)throw std::invalid_argument("Invalid blit shader ABI");
        cbSlot=resource(vs,3,0,4);vbSlot=resource(vs,0,0,4);
        texSlot=resource(ps,0,0,abi==ShaderAbi::GxCopy?4:8);samplerSlot=0;
        if(abi==ShaderAbi::SdkTexture)samplerSlot=resource(ps,2,0,4);
        if(cbSlot<vbSlot+4&&vbSlot<cbSlot+4)throw std::invalid_argument("Overlapping blit vertex resources");
        if(abi==ShaderAbi::SdkTexture&&texSlot<samplerSlot+4&&samplerSlot<texSlot+8)throw std::invalid_argument("Overlapping blit pixel resources");
        if(abi==ShaderAbi::GxCopy&&texSlot!=0)throw std::invalid_argument("GX copy buffer must occupy PS user dwords 0..3");
        if(abi==ShaderAbi::GxCopy){
            auto* ud=field<void*>(ps,8);auto* slots=field<uint16_t*>(ud,8);
            if(!(slots[0]&0x8000)||field<uint16_t>(ud,50)!=0)throw std::invalid_argument("GX copy shader requires a small read-only buffer and no direct sampler");
        }
        allocate(arenas[0]);
        auto* table=sceAgcGetRegisterDefaults();
        if(!table)throw std::runtime_error("AGC register defaults unavailable");
        auto blocks=field<Register**>(table,0);auto count=field<uint32_t>(table,0x20);
        if(!blocks||!blocks[0]||!count||count>3000)throw std::runtime_error("Invalid AGC default context");
        defaults.assign(blocks[0],blocks[0]+count);
        std::sort(defaults.begin(),defaults.end(),[](const auto&a,const auto&b){return a.offset<b.offset;});
        if(sceAgcLinkShaders(linkage,primitive,nullptr,vs,ps,4)<0)throw std::runtime_error("Blit shader linkage failed");
    }
    uint32_t value(uint16_t offset) const {
        auto it=std::lower_bound(defaults.begin(),defaults.end(),offset,
            [](const auto& r,uint16_t o){return r.offset<o;});
        if(it==defaults.end()||it->offset!=offset)throw std::runtime_error("Required AGC default register missing");
        return it->value;
    }
    void allocate(Arena& a){
        const auto pool=sceKernelGetDirectMemorySize();
        if(pool>INT64_MAX||pool<Bytes||sceKernelAllocateDirectMemory(0,int64_t(pool),Bytes,Alignment,12,&a.physical)<0)
            throw std::runtime_error("Blit direct allocation failed");
        if(sceKernelMapDirectMemory(&a.memory,Bytes,0x33,0,a.physical,Alignment)<0||!a.memory){
            if(sceKernelReleaseDirectMemory(a.physical,Bytes))std::fputs("[mkw-blit] map rollback release failed\n",stderr);
            a.physical=-1;a.memory=nullptr;throw std::runtime_error("Blit direct mapping failed");
        }
        std::memset(a.memory,0,Bytes);
    }
    ~Impl(){
        for(auto& a:arenas){
            if(a.memory&&sceKernelMunmap(a.memory,Bytes)){std::fputs("[mkw-blit] unmap failed; allocation retained\n",stderr);continue;}
            if(a.physical>=0&&sceKernelReleaseDirectMemory(a.physical,Bytes))std::fputs("[mkw-blit] physical release failed\n",stderr);
        }
    }
    template<class T> T* at(size_t offset){return reinterpret_cast<T*>(static_cast<char*>(current->memory)+offset);}
    // Non-blocking: release the owners of every blit the GPU has completed.
    void retire(){for(auto& a:arenas)if(a.submitted&&a.landed())a.release();}
    bool unresolved() const {
        if(failed)return true;
        for(const auto& a:arenas)if(a.submitted&&!a.landed())return true;
        return false;
    }
    void wait(Arena& a){
        const auto started=gpu_clock_nanos();
        struct Account{uint64_t started;~Account(){gx_perf_add(gx_perf_stats().blitWaitNanos,gpu_clock_nanos()-started);gx_perf_add(gx_perf_stats().blitWaits,1);}} account{started};
        for(unsigned tries=0;tries<5000;++tries){
            if(a.landed()){a.release();return;}
            if(tries<32){
                // The label usually lands within tens to hundreds of microseconds.
                // A kernel sleep lasts >= ~250 us here, so poll with the CPU pause
                // instruction first, for the 32 attempts before the first sleep.
                for(unsigned spin=0;spin<kSpinPollsPerAttempt;++spin){
                    if((spin&63)==63&&a.landed())break;
                    __builtin_ia32_pause();
                }
                gpu_wait_slice(0);
            }else{
                gpu_wait_slice(tries<128?200:1000);
            }
        }
        failed=true;throw std::runtime_error("Blit completion timeout; resources retained");
    }
    // Waits for every submitted blit, oldest first.
    void finish(){
        if(failed)throw std::logic_error("Blit submission unresolved; resources retained");
        for(;;){
            Arena* oldest=nullptr;
            for(auto& a:arenas)if(a.submitted&&(!oldest||a.serial<oldest->serial))oldest=&a;
            if(!oldest)return;
            wait(*oldest);
        }
    }
    // A free arena, allocating one while fewer than InFlight exist; otherwise
    // waits for the oldest submission to complete.
    Arena& claim(){
        retire();
        for(auto& a:arenas)if(a.memory&&!a.submitted)return a;
        for(auto& a:arenas)if(!a.memory){allocate(a);return a;}
        Arena* oldest=nullptr;
        for(auto& a:arenas)if(!oldest||a.serial<oldest->serial)oldest=&a;
        wait(*oldest);
        return *oldest;
    }
    uint64_t execute(std::shared_ptr<const GpuColorTarget> src,std::shared_ptr<GpuColorTarget> dst,BlitSampleRect s,BlitRect d,const GxCopyOptions* options,uint32_t writeMask=15,
        std::shared_ptr<GpuDepthTarget> depth=nullptr,DepthTest test={},std::shared_ptr<const GpuDepthTarget> depthSrc=nullptr) {
        if(failed)throw std::logic_error("Blit submission unresolved; resources retained");
        if(depthSrc){
            if(!depthSrc->data())throw std::invalid_argument("Depth copy requires a live depth source");
        }else if(!src||!src->data()||(dst&&src->data()==dst->data()))throw std::invalid_argument("Blit requires a live color source");
        if(!dst||!dst->data())throw std::invalid_argument("Blit requires a live destination");
        const unsigned sourceWidth=src?src->layout().width():depthSrc->layout().width();
        const unsigned sourceHeight=src?src->layout().height():depthSrc->layout().height();
        auto vertices=sampled_blit_vertices(sourceWidth,sourceHeight,dst->layout().width(),dst->layout().height(),s,d);
        if(depth){
            if(!depth->data()||depth->layout().width()!=dst->layout().width()||depth->layout().height()!=dst->layout().height()||
                !std::isfinite(test.value)||test.value<0||test.value>1||uint32_t(test.compare)>7||writeMask>15)
                throw std::invalid_argument("Invalid blit depth attachment/state");
            for(auto& vertex:vertices)vertex.position[2]=test.value;
        }
        GxCopyOptions defaultOptions;const auto& opt=options?*options:defaultOptions;
        if(options&&abi!=ShaderAbi::GxCopy)throw std::invalid_argument("Copy options require GX copy shader ABI");
        if(depthSrc&&abi!=ShaderAbi::GxCopy)throw std::invalid_argument("Depth copy requires the GX copy shader ABI");
        const bool formatOk=depthSrc?mkw_copy_depth_format(opt.format):mkw_copy_color_format(opt.format);
        if(!formatOk||!std::isfinite(opt.rowStride)||opt.rowStride<=0||opt.rowStride>16384||
            opt.filter[0]>126||opt.filter[1]>441||opt.filter[2]>126)throw std::invalid_argument("Unsupported GX copy options");
        if(serial==UINT64_MAX)throw std::overflow_error("Blit completion serial exhausted");
        Arena& arena=claim();current=&arena;
        // Disjoint arena slices: DCB 0..64K, context 64K..96K, SH 96K..100K,
        // primitive 100K, geometry 104K, constants 108K, completion 112K.
        auto* cx=at<Register>(65536);unsigned cxCount=0;
        auto add=[&](Register r){if(cxCount>=4096)throw std::runtime_error("Blit context capacity exceeded");cx[cxCount++]=r;};
        // Bind the required copy state below, preserving the remaining
        // baseline established by sceAgcInit (see the public contract).
        auto* primitiveSlot=at<Register>(102400);std::memcpy(primitiveSlot,primitive,sizeof(primitive));
        for(auto r:linkage)add(r);
        unsigned shCount=0;auto* sh=at<Register>(98304);
        for(void* shader:{vs,ps}){
            auto nc=field<uint8_t>(shader,91),ns=field<uint8_t>(shader,92);
            auto* cr=field<Register*>(shader,24);auto* sr=field<Register*>(shader,32);
            if((nc&&!cr)||(ns&&!sr)||shCount+ns>512)throw std::runtime_error("Invalid blit shader registers");
            for(unsigned i=0;i<nc;++i)add(cr[i]);
            for(unsigned i=0;i<ns;++i)sh[shCount++]=sr[i];
        }
        std::array<uint32_t,16> rtDefaults;
        for(unsigned i=0;i<16;++i)rtDefaults[i]=value(ColorTargetOffsets[i]);
        auto rt=dst->context_values(rtDefaults);
        for(unsigned i=0;i<16;++i)add({ColorTargetOffsets[i],0,rt[i]});
        if(depth){
            std::array<uint32_t,16> depthDefaults;
            for(unsigned i=0;i<16;++i)depthDefaults[i]=value(DepthTargetOffsets[i]);
            auto drt=depth->context_values(depthDefaults);
            for(unsigned i=0;i<16;++i)add({DepthTargetOffsets[i],0,drt[i]});
            add({0,0,0}); // Normal depth rendering, no fast clear/resolve.
        }
        float width=float(dst->layout().width()),height=float(dst->layout().height());
        auto fp=[&](uint16_t offset,float f){add({offset,0,std::bit_cast<uint32_t>(f)});};
        fp(0x10f,width*.5f);fp(0x110,width*.5f);fp(0x111,-height*.5f);fp(0x112,height*.5f);
        fp(0x113,1);fp(0x114,0);fp(0xb4,0);fp(0xb5,1);
        for(uint16_t offset=0x2fa;offset<=0x2fd;++offset)fp(offset,8);
        add({0x90,0,0x80000000});add({0x91,0,dst->layout().width()|(dst->layout().height()<<16)});
        add({0x8e,0,writeMask});add({0x205,0,value(0x205)&~7u});
        // Stencil and blending disabled; optional D32 comparison/write.
        add({0x200,0,depth?(2u|(test.write?4u:0u)|(uint32_t(test.compare)<<4)):0});add({0x1e0,0,0});
        auto* v=at<BlitVertex>(106496);std::memcpy(v,vertices.data(),sizeof(vertices));
        uint32_t indices[6]={0,1,2,0,2,3};auto* ib=at<uint32_t>(106752);std::memcpy(ib,indices,sizeof(indices));
        auto* constants=at<float>(110592);std::memset(constants,0,128);
        for(unsigned i=0;i<16;i+=5)constants[i]=constants[i+16]=1;
        auto descriptor=[](void* address,unsigned stride,unsigned count,unsigned format){auto a=reinterpret_cast<uintptr_t>(address);
            return std::array<uint32_t,4>{uint32_t(a),uint32_t((a>>32)&65535)|(stride<<16),count,format};};
        auto cb=descriptor(constants,16,8,0xfac|(77<<12)),vb=descriptor(v,36,4,0x204|(5<<12));
        std::array<uint32_t,8> texture{};uint32_t sampler[4]={0x92,0,0x01000000,0};
        if(src)texture=src->texture_descriptor();
        auto* uniforms=at<GxCopyUniforms>(118784);
        if(abi==ShaderAbi::GxCopy){
            uniforms->texture=texture;std::memcpy(uniforms->sampler.data(),sampler,sizeof(sampler));
            if(opt.linear)uniforms->sampler[2]|=(1u<<20)|(1u<<22);
            uniforms->filter={float(opt.filter[0]),float(opt.filter[1]),float(opt.filter[2]),opt.rowStride/sourceHeight};
            uniforms->clamp={opt.clampTop?(s.y+.5f)/sourceHeight:0.f,
                opt.clampBottom?(s.y+s.height-.5f)/sourceHeight:1.f,0,0};
            uniforms->flags={uint32_t(opt.filter!=std::array<uint32_t,3>{0,64,0}),uint32_t(opt.forceOpaqueAlpha),opt.format,uint32_t(depthSrc?1:0)};
            if(depthSrc){
                const auto a=reinterpret_cast<uintptr_t>(depthSrc->data());
                if(a>=(uint64_t{1}<<48)||depthSrc->allocation_size()>UINT32_MAX)
                    throw std::invalid_argument("GX depth copy source exceeds descriptor range");
                uniforms->depth={uint32_t(a),uint32_t((a>>32)&65535),uint32_t(depthSrc->allocation_size()),0x30005204u};
                uniforms->dims={depthSrc->layout().width(),depthSrc->layout().height(),depthSrc->layout().padded_width()>>7,0};
            }else{uniforms->depth={};uniforms->dims={};}
        }
        bool overflow=false;auto* words=at<uint32_t>(0);
        Dcb commands{words,words+16384,words,words+16384,reinterpret_cast<intptr_t>(&exhausted),&overflow,0,0};
        sceAgcDcbSetCxRegistersIndirect(&commands,cx,cxCount);sceAgcDcbSetShRegistersIndirect(&commands,sh,shCount);
        sceAgcDcbSetUcRegistersIndirect(&commands,primitiveSlot,3);
        sceAgcCbSetShRegisterRangeDirect(&commands,0x8c+cbSlot,cb.data(),4);
        sceAgcCbSetShRegisterRangeDirect(&commands,0x8c+vbSlot,vb.data(),4);
        if(abi==ShaderAbi::GxCopy){
            auto binding=descriptor(uniforms,0,sizeof(GxCopyUniforms),0x204|(5<<12));binding[3]|=3u<<28;
            sceAgcCbSetShRegisterRangeDirect(&commands,0xc+texSlot,binding.data(),4);
        }else{
            sceAgcCbSetShRegisterRangeDirect(&commands,0xc+texSlot,texture.data(),8);
            sceAgcCbSetShRegisterRangeDirect(&commands,0xc+samplerSlot,sampler,4);
        }
        sceAgcDcbSetIndexSize(&commands,1,0);sceAgcDcbSetIndexBuffer(&commands,ib);
        sceAgcDcbSetIndexCount(&commands,6);sceAgcDcbDrawIndex(&commands,6,ib,0);
        if(overflow||commands.down-commands.up<8)throw std::runtime_error("Blit DCB capacity exceeded");
        auto* label=at<uint64_t>(114688);*label=0;
        auto packet=encode_gpu_completion(reinterpret_cast<uintptr_t>(label),serial+1);
        std::memcpy(commands.up,packet.data(),sizeof(packet));commands.up+=8;
        // Flush only the arena ranges actually written: command words, the
        // indirect register blocks, geometry, constants, completion and the
        // GX copy uniform block.
        auto flushRange=[&](size_t offset,size_t bytes){
            for(size_t i=0;i<bytes;i+=64)__builtin_ia32_clflush(at<char>(offset)+i);};
        flushRange(0,size_t(commands.up-words)*4);flushRange(65536,cxCount*8);flushRange(98304,shCount*8);
        flushRange(102400,24);flushRange(106496,sizeof(vertices));flushRange(106752,24);
        flushRange(110592,128);flushRange(114688,8);flushRange(118784,sizeof(GxCopyUniforms));
        __atomic_thread_fence(__ATOMIC_SEQ_CST);
        struct {void* words;uint32_t count;uint8_t flag;} submit{};
        submit.words=words;submit.count=uint32_t(commands.up-words);
        arena.source=std::move(src);arena.target=std::move(dst);arena.depthTarget=std::move(depth);arena.depthSource=std::move(depthSrc);
        arena.serial=++serial;arena.submitted=true;
        // Even a submit error is conservatively unresolved: don't reclaim a
        // buffer which the driver may have partially accepted.
        auto& stats=gx_perf_stats();
        const auto submitStarted=gpu_clock_nanos();
        if(sceAgcDriverSubmitDcb(&submit)<0){failed=true;throw std::runtime_error("Blit submit failed; resources retained");}
        const auto suspendStarted=gpu_clock_nanos();
        // A suspend point after every blit is required: deferring it to the next
        // draw batch, CPU wait or frame end (measured build a7ea8d7c) rendered the
        // race with blocky stale screen-effect copies; one per frame only (4488e43f)
        // left submitted draws unfinished (GX draw completion timeout).
        sceAgcSuspendPoint();
        gx_perf_add(stats.blitSubmitNanos,suspendStarted-submitStarted);gx_perf_add(stats.blitSuspendNanos,gpu_clock_nanos()-suspendStarted);
        // Asynchronous: GPU order follows submission order, so later draws and
        // blits reading this destination are correct. CPU readers call finish().
        retire();
        return serial;
    }
};
AgcBlit::AgcBlit(void* vs,void* ps,ShaderAbi abi):impl_(std::make_unique<Impl>(vs,ps,abi)){}
AgcBlit::~AgcBlit(){
    if(!impl_)return;
    try{if(!impl_->failed)impl_->finish();}catch(...){}
    if(impl_->unresolved()){std::fputs("[mkw-blit] unresolved GPU work retained until process cleanup\n",stderr);(void)impl_.release();}
}
bool AgcBlit::pending() const noexcept{return impl_->unresolved();}
void AgcBlit::finish(){impl_->finish();}
uint64_t AgcBlit::copy(std::shared_ptr<const GpuColorTarget> s,std::shared_ptr<GpuColorTarget> d,BlitRect sr,BlitRect dr,const GxCopyOptions* options){
    return copy_sampled(std::move(s),std::move(d),{float(sr.x),float(sr.y),float(sr.width),float(sr.height)},dr,options);
}
namespace{struct BlitTimer{uint64_t started=gpu_clock_nanos();~BlitTimer(){gx_perf_add(gx_perf_stats().blitNanos,gpu_clock_nanos()-started);gx_perf_add(gx_perf_stats().blitCount,1);}};}
uint64_t AgcBlit::copy_sampled(std::shared_ptr<const GpuColorTarget> s,std::shared_ptr<GpuColorTarget> d,BlitSampleRect sr,BlitRect dr,const GxCopyOptions* options){
    BlitTimer timer;
    return impl_->execute(std::move(s),std::move(d),sr,dr,options);
}
uint64_t AgcBlit::copy_depth(std::shared_ptr<const GpuDepthTarget> s,std::shared_ptr<GpuColorTarget> d,BlitSampleRect sr,BlitRect dr,const GxCopyOptions* options){
    BlitTimer timer;gx_perf_add(gx_perf_stats().blitDepthCopies,1);
    return impl_->execute(nullptr,std::move(d),sr,dr,options,15,nullptr,{},std::move(s));
}
uint64_t AgcBlit::clear_color(std::shared_ptr<GpuColorTarget> dst,BlitRect rect,uint32_t bgra,uint32_t writeMask){
    if(impl_->failed)throw std::logic_error("Blit submission unresolved; resources retained");
    if(!dst||!dst->data()||!writeMask||writeMask>15)throw std::invalid_argument("Invalid GPU color clear");
    (void)blit_vertices(1,1,dst->layout().width(),dst->layout().height(),{0,0,1,1},rect);
    auto constant=impl_->fill_constant(bgra);
    BlitTimer timer;gx_perf_add(gx_perf_stats().blitClears,1);
    return impl_->execute(std::move(constant),std::move(dst),{0,0,1,1},rect,nullptr,writeMask);
}
uint64_t AgcBlit::fill_depth_tested(std::shared_ptr<GpuColorTarget> dst,std::shared_ptr<GpuDepthTarget> depth,
    BlitRect rect,uint32_t bgra,DepthTest test,uint32_t writeMask){
    if(impl_->failed)throw std::logic_error("Blit submission unresolved; resources retained");
    if(!dst||!dst->data()||!depth||!depth->data()||depth->layout().width()!=dst->layout().width()||
        depth->layout().height()!=dst->layout().height()||!std::isfinite(test.value)||test.value<0||test.value>1||
        uint32_t(test.compare)>7||writeMask>15)throw std::invalid_argument("Invalid GPU depth fill");
    (void)blit_vertices(1,1,dst->layout().width(),dst->layout().height(),{0,0,1,1},rect);
    auto constant=impl_->fill_constant(bgra);
    BlitTimer timer;gx_perf_add(gx_perf_stats().blitClears,1);
    return impl_->execute(std::move(constant),std::move(dst),{0,0,1,1},rect,nullptr,writeMask,std::move(depth),test);
}
}
