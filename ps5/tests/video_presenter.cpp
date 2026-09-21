// SPDX-License-Identifier: GPL-3.0-only
// Actual presenter, AgcBlit and allocations; this file substitutes VideoOut.
#include "video_presenter.h"
#include <array>
#include <cstring>
#include <stdexcept>
#include <limits>
using namespace mkw::agc;
namespace {
unsigned checks,flips,reads;bool outputOpen,registered,waiting,failOpen,failRegister,failClose,failFlip,failStatus,timeout;
int current=-1,next;int64_t arg,nextArg;uint64_t count;
std::array<void*,2> pointers{};
void check(bool value){++checks;if(!value)throw std::runtime_error("VideoOut presenter invariant");}
struct Buffer{void* data;void* metadata;void* reserved0;void* reserved1;};
struct Status{uint64_t count,time,reserved0;int64_t arg;uint64_t reserved1,counter;int graphics,pending,current;uint32_t reserved2;uint64_t submitted;uint8_t padding[56];};
template<class F>void rejects(F f){bool rejected=false;try{f();}catch(const std::exception&){rejected=true;}check(rejected);}
}
bool video_owns_pointer(void* p){return registered&&(p==pointers[0]||p==pointers[1]);}
extern "C" int sceVideoOutOpen(int user,int bus,int index,const void* p){
    check(user==255&&bus==0&&index==0&&!p&&!outputOpen);if(failOpen)return -1;
    outputOpen=true;waiting=false;count=0;arg=0;current=-1;return 17;
}
extern "C" int sceVideoOutClose(int h){check(h==17&&outputOpen);if(failClose)return -1;outputOpen=registered=waiting=false;pointers={};return 0;}
extern "C" int sceVideoOutSetFlipRate(int h,int rate){check(h==17&&outputOpen&&rate==0);return 0;}
extern "C" void sceVideoOutSetBufferAttribute2(void* attr,uint64_t format,unsigned tile,unsigned w,unsigned h,uint64_t opt,unsigned dcc,uint64_t clear){
    check(format==0x8000000000000000ull&&tile==0&&w==64&&h==36&&opt==0&&dcc==0&&clear==0);
    // Full native structure size from SharpProspero, including three reserved
    // u64 fields. A 64-byte SDK-example allocation corrupts neighbouring data.
    std::memset(attr,0x55,80);
}
extern "C" int sceVideoOutRegisterBuffers2(int h,int start,int set,const Buffer* b,int n,const void* attr,int category,const void* extra){
    check(h==17&&outputOpen&&start==0&&set==0&&n==2&&attr&&category==0&&!extra);
    if(failRegister)return -1;
    for(int i=0;i<2;++i){check(b[i].data&&!b[i].metadata&&!b[i].reserved0&&!b[i].reserved1);pointers[i]=b[i].data;}
    check(pointers[0]!=pointers[1]);registered=true;return 0;
}
extern "C" int sceVideoOutSubmitFlip(int h,int slot,unsigned mode,int64_t a){
    check(h==17&&registered&&mode==1&&slot>=0&&slot<2&&slot!=current&&!waiting);
    ++flips;waiting=true;next=slot;nextArg=a;reads=0;return failFlip?-1:0;
}
extern "C" int sceVideoOutGetFlipStatus(int h,Status* s){
    check(h==17&&outputOpen);if(failStatus)return -1;*s={};
    if(waiting){
        ++reads;s->count=count+1;s->arg=nextArg;s->current=next;
        if(timeout||reads==1){s->arg=nextArg-1;return 0;}
        if(reads==2){s->current=next==0?1:0;return 0;}
        if(reads==3){s->pending=1;return 0;}
        waiting=false;current=next;arg=nextArg;++count;
    }
    s->count=count;s->arg=arg;s->current=current;s->time=count*16667;return 0;
}
unsigned test_video_presenter(void* vs,void* ps){
    check(fit_present_rect(1920,1080,1).x==420);
    auto r=fit_present_rect(1920,1080,4.f/3);check(r.x==240&&r.width==1440&&r.height==1080);
    r=fit_present_rect(1920,1080,16.f/9);check(r.x==0&&r.y==0&&r.width==1920&&r.height==1080);
    rejects([&]{fit_present_rect(0,1080,1);});rejects([&]{fit_present_rect(1920,1080,std::numeric_limits<float>::quiet_NaN());});
    AgcBlit blit(vs,ps,AgcBlit::ShaderAbi::GxCopy);auto source=std::make_shared<GpuColorTarget>(32,32);
    failOpen=true;rejects([&]{VideoPresenter p(blit,64,36);});failOpen=false;check(!registered&&!outputOpen);
    failRegister=true;rejects([&]{VideoPresenter p(blit,64,36);});failRegister=false;check(!registered&&!outputOpen);
    {
        VideoPresenter p(blit,64,36);rejects([&]{p.current_target();});rejects([&]{p.present(nullptr,1);});
        for(unsigned i=0;i<8;++i){auto frame=p.present(source,i%2?16.f/9:1);
            check(frame.serial==i+1&&frame.count==i+1&&frame.buffer==int(i%2)&&reads==4);
            check(p.current_target().data()==pointers[frame.buffer]&&source.use_count()==1);}
        check(p.close()==0&&!registered&&!outputOpen);rejects([&]{p.present(source,1);});
    }
    // Submit mode returns before the flip is on screen and retires it before the next flip.
    {
        VideoPresenter p(blit,64,36);
        for(unsigned i=0;i<4;++i){auto frame=p.submit(source,1);
            check(frame.serial==i+1&&frame.count==i&&frame.buffer==int(i%2)&&waiting&&reads==0);
            rejects([&]{p.current_target();});}
        check(p.retire_pending()&&!waiting&&p.current_target().data()==pointers[1]&&!p.retire_pending());
        check(p.present(source,1).count==5&&p.close()==0&&!registered);
    }
    for(unsigned failure=0;failure<3;++failure){
        VideoPresenter p(blit,64,36);failFlip=failure==0;timeout=failure==1;failStatus=failure==2;
        unsigned before=flips;rejects([&]{p.present(source,1);});check(registered);
        rejects([&]{p.present(source,1);});check(flips==before+(failure!=2));
        failFlip=timeout=failStatus=false;check(p.close()==0&&!registered);
    }
    // Destruction must not release registered GPU buffers when close fails.
    {VideoPresenter p(blit,64,36);p.present(source,1);failClose=true;}
    check(outputOpen&&registered);failClose=false;outputOpen=registered=false;pointers={};
    return checks;
}

