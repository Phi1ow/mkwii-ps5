// SPDX-License-Identifier: GPL-3.0-only
// Real arena, resource ownership and completion code; simulated kernel/driver.
#include <vector>
#include <string>
#include "agc_blit.h"
#include <array>
#include <cstdio>
#include <cstring>
#include <map>
#include <malloc.h>
#include <stdexcept>
#include <limits>
using namespace mkw::agc;
#ifdef MKW_VIDEO_PRESENTER_TEST
unsigned test_video_presenter(void*,void*);
bool video_owns_pointer(void*);
#endif
static unsigned checks,submissions,polls,draws;
static bool failAllocate,failMap,failSubmit,timeout;
// Completion labels written later by the test instead of at submission.
static bool deferCompletion;static std::vector<std::pair<uint64_t*,uint64_t>> deferredLabels;
static bool copyAbi;
static GxCopyUniforms observedUniforms;
static uint32_t observedMask;
static std::map<uint16_t,uint32_t> observedContext;
static std::array<BlitVertex,4> observedVertices;
static void check(bool v,unsigned line=__builtin_LINE()){++checks;if(!v)throw std::runtime_error("Blit ownership/command invariant failed at line "+std::to_string(line));}
struct Allocation{size_t size,alignment;void* mapping=nullptr;};
static std::map<int64_t,Allocation> allocations;
static int64_t nextPhysical;
extern "C" uint64_t sceKernelGetDirectMemorySize(){return 1ull<<32;}
extern "C" int sceKernelAllocateDirectMemory(int64_t,int64_t,size_t size,size_t alignment,int type,int64_t* out){
    check(type==12&&size%alignment==0);if(failAllocate)return -1;
    *out=nextPhysical;nextPhysical+=size;allocations.emplace(*out,Allocation{size,alignment});return 0;
}
extern "C" int sceKernelMapDirectMemory(void** out,size_t size,int prot,int flags,int64_t physical,size_t alignment){
    check(prot==0x33&&flags==0&&allocations.at(physical).size==size&&allocations.at(physical).alignment==alignment);
    if(failMap)return -1;*out=_aligned_malloc(size,alignment);check(*out!=nullptr);allocations.at(physical).mapping=*out;return 0;
}
extern "C" int sceKernelMunmap(void* address,size_t size){
#ifdef MKW_VIDEO_PRESENTER_TEST
    check(!video_owns_pointer(address));
#endif
    for(auto& [physical,a]:allocations){(void)physical;if(a.mapping==address){check(a.size==size);_aligned_free(address);a.mapping=nullptr;return 0;}}
    check(false);return -1;
}
extern "C" int sceKernelReleaseDirectMemory(int64_t physical,size_t size){
    check(allocations.at(physical).size==size&&!allocations.at(physical).mapping);allocations.erase(physical);return 0;
}
extern "C" int sceKernelUsleep(unsigned){++polls;return 0;}
struct Register{uint16_t offset,pad;uint32_t value;};
struct Dcb{uint32_t *bottom,*top,*up,*down;intptr_t callback;void* user;uint32_t reserved,pad;};
static std::array<Register,33> context;
static std::array<unsigned char,64> defaults;
extern "C" void* sceAgcGetRegisterDefaults(){return defaults.data();}
extern "C" int sceAgcLinkShaders(void* linkage,void* primitive,void*,void*,void*,unsigned type){
    check(type==4);std::memset(linkage,0,34*sizeof(Register));std::memset(primitive,0,3*sizeof(Register));return 0;
}
extern "C" void sceAgcDcbSetCxRegistersIndirect(void*,void* records,unsigned count){
    check(records&&count>34);
    observedContext.clear();auto* r=static_cast<Register*>(records);for(unsigned i=0;i<count;++i)observedContext[r[i].offset]=r[i].value;observedMask=observedContext.at(0x8e);
}
extern "C" void sceAgcDcbSetShRegistersIndirect(void*,void*,unsigned count){check(count==0);}
extern "C" void sceAgcDcbSetUcRegistersIndirect(void*,void*,unsigned count){check(count==3);}
extern "C" void sceAgcCbSetShRegisterRangeDirect(void*,unsigned offset,unsigned* words,unsigned count){
    if(offset==0x90){auto address=uint64_t(words[0])|(uint64_t(words[1]&65535)<<32);std::memcpy(observedVertices.data(),reinterpret_cast<void*>(address),sizeof(observedVertices));}
    if(copyAbi&&offset==0xc){check(count==4&&words[2]==sizeof(GxCopyUniforms)&&(words[3]>>28)==3);
        auto address=uint64_t(words[0])|(uint64_t(words[1]&65535)<<32);
        observedUniforms=*reinterpret_cast<GxCopyUniforms*>(address);return;}
    check((offset==0x8c&&count==4)||(offset==0x90&&count==4)||(offset==0xc&&count==8)||(offset==0x14&&count==4));
}
extern "C" void* sceAgcDcbSetIndexSize(void*,unsigned char size,unsigned char cache){check(size==1&&!cache);return nullptr;}
extern "C" void* sceAgcDcbSetIndexBuffer(void*,void* p){check(p!=nullptr);return nullptr;}
extern "C" void* sceAgcDcbSetIndexCount(void*,unsigned count){check(count==6);return nullptr;}
extern "C" void* sceAgcDcbDrawIndex(void* opaque,unsigned count,void*,uint64_t modifier){
    check(count==6&&!modifier);++draws;auto* d=static_cast<Dcb*>(opaque);*d->up++=0x1234;return nullptr;
}
extern "C" int sceAgcDriverSubmitDcb(void* description){
    struct Submit{uint32_t* words;uint32_t count;uint8_t flag;};auto* s=static_cast<Submit*>(description);
    check(s->count==9&&s->flag==0&&s->words[1]==0xc0064900);++submissions;
    if(failSubmit)return -1;
    auto* p=s->words+1;uint64_t address=uint64_t(p[3])|(uint64_t(p[4])<<32);
    if(deferCompletion)deferredLabels.push_back({reinterpret_cast<uint64_t*>(address),uint64_t(p[5])|(uint64_t(p[6])<<32)});
    else if(!timeout)*reinterpret_cast<uint64_t*>(address)=uint64_t(p[5])|(uint64_t(p[6])<<32);
    return 0;
}
extern "C" int sceAgcSuspendPoint(){return 0;}
template<class T> static void put(void* memory,size_t offset,T value){std::memcpy(static_cast<char*>(memory)+offset,&value,sizeof(value));}
template<class F> static void rejects(F action,unsigned line=__builtin_LINE()){bool rejected=false;try{action();}catch(const std::exception&){rejected=true;}check(rejected,line);}
int main(){try{
    for(unsigned i=0;i<16;++i)context[i]={ColorTargetOffsets[i],0,0};context[16]={0x205,0,0};
    for(unsigned i=0;i<16;++i)context[17+i]={DepthTargetOffsets[i],0,0};
    Register* records=context.data();put(defaults.data(),0,&records);put(defaults.data(),0x20,uint32_t(context.size()));
    std::array<unsigned char,128> vs{},ps{},vu{},pu{};
    uint16_t vertex=4,constant=0,texture=0,sampler=8;
    put(vs.data(),8,vu.data());put(ps.data(),8,pu.data());
    put(vu.data(),8,&vertex);put(vu.data(),32,&constant);put(vu.data(),46,uint16_t(1));put(vu.data(),52,uint16_t(1));
    put(pu.data(),8,&texture);put(pu.data(),24,&sampler);put(pu.data(),46,uint16_t(1));put(pu.data(),50,uint16_t(1));
    rejects([&]{AgcBlit invalid(nullptr,ps.data());});check(allocations.empty());
    failAllocate=true;rejects([&]{AgcBlit failed(vs.data(),ps.data());});failAllocate=false;check(allocations.empty());
    failMap=true;rejects([&]{AgcBlit failed(vs.data(),ps.data());});failMap=false;check(allocations.empty());
    auto src=std::make_shared<GpuColorTarget>(64,32),dst=std::make_shared<GpuColorTarget>(32,16);
    {AgcBlit blit(vs.data(),ps.data());check(allocations.size()==3);
        rejects([&]{blit.copy(src,src,{0,0,64,32},{0,0,64,32});});
        rejects([&]{blit.copy(src,dst,{63,0,2,1},{0,0,32,16});});check(submissions==0);
        check(blit.copy(src,dst,{0,0,64,32},{0,0,32,16})==1);
        check(blit.copy(src,dst,{16,8,32,16},{0,0,32,16})==2);
        check(submissions==2&&draws==2&&polls==0&&src.use_count()==1&&dst.use_count()==1);
    }check(allocations.size()==2);
    // Asynchronous blits: three submissions in flight without waiting, each with its own arena and
    // retained owners; completion releases them and finish() then returns without polling.
    {AgcBlit blit(vs.data(),ps.data());deferCompletion=true;auto submitted=submissions;
        for(unsigned i=0;i<3;++i)check(blit.copy(src,dst,{0,0,64,32},{0,0,32,16})==i+1);
        check(submissions==submitted+3&&blit.pending()&&src.use_count()==4&&dst.use_count()==4&&allocations.size()==5&&polls==0);
        for(auto& [label,value]:deferredLabels)*label=value;
        deferredLabels.clear();deferCompletion=false;
        blit.finish();check(!blit.pending()&&src.use_count()==1&&dst.use_count()==1&&polls==0);
        check(blit.copy(src,dst,{0,0,64,32},{0,0,32,16})==4&&allocations.size()==5);
    }check(allocations.size()==2);
    rejects([&]{AgcBlit invalid(vs.data(),ps.data(),AgcBlit::ShaderAbi(99));});
    rejects([&]{AgcBlit invalid(vs.data(),ps.data(),AgcBlit::ShaderAbi::GxCopy);});
    texture=0x8000;put(pu.data(),50,uint16_t(0));copyAbi=true;
    {AgcBlit blit(vs.data(),ps.data(),AgcBlit::ShaderAbi::GxCopy);GxCopyOptions opt;
        auto before=submissions;
        opt.format=0x10;rejects([&]{blit.copy(src,dst,{0,0,64,32},{0,0,32,16},&opt);});opt.format=3;
        opt.rowStride=std::numeric_limits<float>::quiet_NaN();rejects([&]{blit.copy(src,dst,{0,0,64,32},{0,0,32,16},&opt);});
        opt.rowStride=0;rejects([&]{blit.copy(src,dst,{0,0,64,32},{0,0,32,16},&opt);});opt.rowStride=2;
        opt.filter={127,64,0};rejects([&]{blit.copy(src,dst,{0,0,64,32},{0,0,32,16},&opt);});check(submissions==before);
        opt.filter={16,32,16};opt.linear=true;opt.forceOpaqueAlpha=true;opt.clampTop=opt.clampBottom=true;
        check(blit.copy(src,dst,{8,4,32,16},{0,0,32,16},&opt)==1);
        check((observedUniforms.filter==std::array<float,4>{16,32,16,.0625f}));
        check((observedUniforms.clamp==std::array<float,4>{4.5f/32,19.5f/32,0,0}));
        check((observedUniforms.flags==std::array<uint32_t,4>{1,1,3,0}));
        check(observedUniforms.sampler[2]==0x01500000&&src.use_count()==1&&dst.use_count()==1);
        opt.rowStride=.5f;
        check(blit.copy_sampled(src,dst,{8.25f,4.5f,31.5f,15.25f},{0,0,32,16},&opt)==2);
        check((observedUniforms.filter==std::array<float,4>{16,32,16,.5f/32}));
        check((observedUniforms.clamp==std::array<float,4>{5.f/32,19.25f/32,0,0}));
        auto beforeClear=submissions;
        rejects([&]{blit.clear_color(dst,{0,0,32,16},0x40102030,0);});
        rejects([&]{blit.clear_color(dst,{0,0,32,16},0x40102030,16);});
        rejects([&]{blit.clear_color(dst,{31,0,2,16},0x40102030);});
        failAllocate=true;rejects([&]{blit.clear_color(dst,{0,0,32,16},0x40102030);});failAllocate=false;
        check(submissions==beforeClear&&allocations.size()==3);
        for(uint32_t mask=1;mask<16;++mask){
            check(blit.clear_color(dst,{3,2,17,9},0x40102030,mask)==mask+2);
            // The 1x1 fill constant is allocated by the first clear and reused by every later one.
            check(observedMask==mask&&allocations.size()==4&&dst.use_count()==1);
        }
        auto depth=std::make_shared<GpuDepthTarget>(32,16);
        auto wrongDepth=std::make_shared<GpuDepthTarget>(33,16);beforeClear=submissions;
        rejects([&]{blit.fill_depth_tested(dst,nullptr,{0,0,32,16},0,{});});
        rejects([&]{blit.fill_depth_tested(dst,wrongDepth,{0,0,32,16},0,{});});
        rejects([&]{blit.fill_depth_tested(dst,depth,{0,0,32,16},0,{-1,DepthCompare::Less,true});});
        rejects([&]{blit.fill_depth_tested(dst,depth,{0,0,32,16},0,{2,DepthCompare::Less,true});});
        rejects([&]{blit.fill_depth_tested(dst,depth,{0,0,32,16},0,{std::numeric_limits<float>::quiet_NaN(),DepthCompare::Less,true});});
        rejects([&]{blit.fill_depth_tested(dst,depth,{0,0,32,16},0,{.5f,DepthCompare(8),true});});
        rejects([&]{blit.fill_depth_tested(dst,depth,{0,0,32,16},0,{},16);});
        rejects([&]{blit.fill_depth_tested(dst,depth,{31,0,2,16},0,{});});
        check(submissions==beforeClear&&allocations.size()==6);wrongDepth.reset();
        for(unsigned compare=0;compare<8;++compare)for(bool write:{false,true}){
            check(blit.fill_depth_tested(dst,depth,{3,2,17,9},0xff102030,{.25f,DepthCompare(compare),write},0)==18+compare*2+write);
            check(observedMask==0&&observedContext.at(0x200)==(2u|(write?4u:0)|(compare<<4)));
            check(observedContext.at(0x12)==uint32_t(reinterpret_cast<uintptr_t>(depth->data())>>8));
            check(observedContext.at(0x14)==observedContext.at(0x12));check(observedContext.at(7)==(31u|(15u<<16)));
            for(auto v:observedVertices)check(v.position[2]==.25f);
            check(depth.use_count()==1&&allocations.size()==5);
        }
        // Returning to color-only work explicitly disables the depth test.
        blit.copy(src,dst,{0,0,64,32},{0,0,32,16});check(observedContext.at(0x200)==0);
    }check(allocations.size()==2);src.reset();dst.reset();check(allocations.empty());
#ifdef MKW_VIDEO_PRESENTER_TEST
    auto savedPolls=polls;
    std::printf("PASS %u VideoOut presenter state/ownership checks\n",test_video_presenter(vs.data(),ps.data()));
    polls=savedPolls;
    // Failed VideoOutClose deliberately retains its scan-out owners. Only the
    // simulated process teardown may free those outstanding test allocations.
    check(allocations.size()==2);
    for(auto& [physical,a]:allocations){(void)physical;_aligned_free(a.mapping);}allocations.clear();
#endif
    copyAbi=false;texture=0;put(pu.data(),50,uint16_t(1));
    // Both a submission error and timeout must retain source, destination and
    // command memory after the public object's destruction. Process cleanup
    // is simulated below; no hardware completion is claimed by these mocks.
    // Blits are asynchronous: a submission error surfaces from the call, a GPU
    // that never reports completion surfaces from finish().
    for(bool submitError:{false,true}){
        src=std::make_shared<GpuColorTarget>(64,32);dst=std::make_shared<GpuColorTarget>(32,16);
        std::weak_ptr<const GpuColorTarget> weakSource=src;std::weak_ptr<GpuColorTarget> weakTarget=dst;
        auto before=submissions;timeout=!submitError;failSubmit=submitError;
        {AgcBlit blit(vs.data(),ps.data());
            if(submitError)rejects([&]{blit.copy(src,dst,{0,0,64,32},{0,0,32,16});});
            else{check(blit.copy(src,dst,{0,0,64,32},{0,0,32,16})==1&&blit.pending());rejects([&]{blit.finish();});}
            check(blit.pending());
            rejects([&]{blit.copy(src,dst,{0,0,64,32},{0,0,32,16});});check(submissions==before+1);}
        src.reset();dst.reset();check(!weakSource.expired()&&!weakTarget.expired());check(allocations.size()==3);
        for(auto& [physical,a]:allocations){(void)physical;_aligned_free(a.mapping);}allocations.clear();
    }
    dst=std::make_shared<GpuColorTarget>(32,16);std::weak_ptr<GpuColorTarget> clearTarget=dst;
    timeout=true;failSubmit=false;
    {AgcBlit blit(vs.data(),ps.data());check(blit.clear_color(dst,{3,2,17,9},0x40102030,8)==1);
        rejects([&]{blit.finish();});check(allocations.size()==3);
        rejects([&]{blit.clear_color(dst,{0,0,32,16},0,15);});check(allocations.size()==3);}
    dst.reset();check(!clearTarget.expired()&&allocations.size()==3);
    for(auto& [physical,a]:allocations){(void)physical;_aligned_free(a.mapping);}allocations.clear();
    for(bool submitError:{false,true}){
        dst=std::make_shared<GpuColorTarget>(32,16);auto depth=std::make_shared<GpuDepthTarget>(32,16);
        std::weak_ptr<GpuDepthTarget> weakDepth=depth;std::weak_ptr<GpuColorTarget> weakColor=dst;
        timeout=!submitError;failSubmit=submitError;auto before=submissions;
        {AgcBlit blit(vs.data(),ps.data());
            if(submitError)rejects([&]{blit.fill_depth_tested(dst,depth,{0,0,32,16},0,{});});
            else{check(blit.fill_depth_tested(dst,depth,{0,0,32,16},0,{})==1);rejects([&]{blit.finish();});}
            rejects([&]{blit.fill_depth_tested(dst,depth,{0,0,32,16},0,{});});check(submissions==before+1);}
        depth.reset();dst.reset();check(!weakDepth.expired()&&!weakColor.expired()&&allocations.size()==4);
        for(auto& [physical,a]:allocations){(void)physical;_aligned_free(a.mapping);}allocations.clear();
    }
    // Three timeout paths, each 5000 attempts. The wait ladder in agc_blit.cpp
    // spins the guest callback for the first 32 attempts without sleeping, so
    // only the remaining attempts reach usleep. Keep these in step.
    constexpr unsigned kTimeouts=3,kAttempts=5000,kSpinAttempts=32;
    check(polls==kTimeouts*(kAttempts-kSpinAttempts));
   std::printf("PASS %u blit lifetime/command checks; 36 successful submissions, all depth comparisons/write modes, all clear masks, fractional GX options and retained timeout owners\n",checks);return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL %s\n",e.what());return 1;}}
