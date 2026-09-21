// SPDX-License-Identifier: GPL-3.0-only
// VideoOut ABI from the requested SharpProspero VideoOut.cs and SDK GPU example.
#include "video_presenter.h"
#include "gpu_wait_service.h"
#include <array>
#include <cstddef>
#include <cstdio>
#include <stdexcept>
namespace {
struct VideoBuffer {void* data;void* metadata;void* reserved0;void* reserved1;};
struct VideoAttribute {
    int reserved0,tiling,aspect;uint32_t width,height,pitch;
    uint64_t option,format,clear;uint32_t dcc,pad;uint64_t reserved[3];
};
struct FlipStatus {
    uint64_t count,time,reserved0;int64_t arg;uint64_t reserved1,counter;
    int graphics,pending,current;uint32_t reserved2;uint64_t submitted;
    uint8_t padding[56];
};
static_assert(sizeof(VideoBuffer)==32&&sizeof(FlipStatus)==128);
static_assert(sizeof(VideoAttribute)==80&&offsetof(VideoAttribute,format)==32);
static_assert(offsetof(FlipStatus,arg)==24&&offsetof(FlipStatus,pending)==52&&offsetof(FlipStatus,current)==56);
}
extern "C" {
int sceVideoOutOpen(int,int,int,const void*);
int sceVideoOutClose(int);
int sceVideoOutSetFlipRate(int,int);
void sceVideoOutSetBufferAttribute2(void*,uint64_t,unsigned,unsigned,unsigned,uint64_t,unsigned,uint64_t);
int sceVideoOutRegisterBuffers2(int,int,int,const VideoBuffer*,int,const void*,int,const void*);
int sceVideoOutSubmitFlip(int,int,unsigned,int64_t);
int sceVideoOutGetFlipStatus(int,FlipStatus*);
int sceKernelUsleep(unsigned);
}
namespace mkw::agc {
struct VideoPresenter::Impl {
    AgcBlit& blitter;uint32_t width,height;int handle=-1,current=-1;
    std::array<std::shared_ptr<GpuColorTarget>,2> buffers;
    std::array<BlitRect,2> previousRect{};
    uint64_t serial=0,count=0;bool failed=false;
    // Submitted flip not yet observed on screen (submit mode): its slot, rect
    // and the flip count before it.
    bool outstanding=false;int outstandingSlot=-1;uint64_t outstandingBefore=0;BlitRect outstandingRect{};
    Impl(AgcBlit& b,uint32_t w,uint32_t h):blitter(b),width(w),height(h){}
    int close() noexcept{
        if(handle<0)return 0;
        int rc=sceVideoOutClose(handle);
        if(rc<0)return rc;
        handle=-1;current=-1;buffers={};return 0;
    }
    void initialize(){
        (void)fit_present_rect(width,height,1);
        for(auto& buffer:buffers)buffer=std::make_shared<GpuColorTarget>(width,height);
        handle=sceVideoOutOpen(0xff,0,0,nullptr);
        if(handle<0)throw std::runtime_error("VideoOutOpen failed");
        if(sceVideoOutSetFlipRate(handle,0)<0)throw std::runtime_error("VideoOut flip rate failed");
        std::array<VideoBuffer,2> addresses{};for(unsigned i=0;i<2;++i)addresses[i].data=buffers[i]->data();
        VideoAttribute attributes{};
        sceVideoOutSetBufferAttribute2(&attributes,0x8000000000000000ull,0,width,height,0,0,0);
        int rc=sceVideoOutRegisterBuffers2(handle,0,0,addresses.data(),2,&attributes,0,nullptr);
        if(rc<0){char error[192];std::snprintf(error,sizeof(error),"VideoOut buffer registration failed rc=%08x handle=%d extent=%ux%u pitch=%u",unsigned(rc),handle,attributes.width,attributes.height,attributes.pitch);throw std::runtime_error(error);}
    }
    PresentedFrame retire(){
        if(!outstanding)throw std::logic_error("No submitted VideoOut flip to retire");
        for(unsigned attempt=0;attempt<5000;++attempt){
            FlipStatus status{};
            if(sceVideoOutGetFlipStatus(handle,&status)<0){failed=true;throw std::runtime_error("VideoOut retirement status failed");}
            if(status.count>outstandingBefore&&status.arg==int64_t(serial)&&status.pending==0&&status.graphics==0&&status.current==outstandingSlot){
                current=outstandingSlot;count=status.count;outstanding=false;return {serial,count,status.time,current,outstandingRect};
            }
            gpu_wait_slice(1000);
        }
        failed=true;throw std::runtime_error("VideoOut flip timeout; scan-out owners retained");
    }
    PresentedFrame submit(std::shared_ptr<const GpuColorTarget> source,float aspect,bool linear,uint64_t deadline){
        if(handle<0||failed)throw std::logic_error("VideoOut is closed or its previous presentation is unresolved");
        // The previous flip must be on screen before its other buffer is overwritten.
        if(outstanding)(void)retire();
        if(!source||!source->data())throw std::invalid_argument("Missing presentation image");
        auto rect=fit_present_rect(width,height,aspect);
        for(const auto& buffer:buffers)if(source->data()==buffer->data())throw std::invalid_argument("Scan-out cannot be its own presentation source");
        if(serial==INT64_MAX)throw std::overflow_error("VideoOut frame serial exhausted");
        FlipStatus before{};
        if(sceVideoOutGetFlipStatus(handle,&before)<0){failed=true;throw std::runtime_error("VideoOut status failed");}
        if(before.pending||before.graphics||(serial&&(before.current!=current||before.arg!=int64_t(serial)||before.count<count))){
            failed=true;throw std::logic_error("VideoOut changed outside its presenter");
        }
        const int slot=current==0?1:0;
        auto& old=previousRect[slot];
        try{
            if(rect.x||rect.y||rect.width!=width||rect.height!=height){
                if(old.x!=rect.x||old.y!=rect.y||old.width!=rect.width||old.height!=rect.height)
                    blitter.clear_color(buffers[slot],{0,0,width,height},0xff000000);
            }
            GxCopyOptions options;options.linear=linear;
            blitter.copy(source,buffers[slot],{0,0,source->layout().width(),source->layout().height()},rect,&options);
            // Blits are asynchronous: VideoOut must not scan out a buffer the GPU is still writing.
            blitter.finish();
            old=rect;
        }catch(...){failed=true;throw;}
        // The blit's completion fence precedes this flip. No draw writes the
        // current buffer; successful retirement makes the old one reusable.
        ++serial;failed=true;
        if(deadline)gpu_wait_until(deadline);
        if(sceVideoOutSubmitFlip(handle,slot,1,int64_t(serial))<0)
            throw std::runtime_error("VideoOut flip submission failed; scan-out owners retained");
        outstanding=true;outstandingSlot=slot;outstandingBefore=before.count;outstandingRect=rect;failed=false;
        return {serial,count,before.time,slot,rect};
    }
};
VideoPresenter::VideoPresenter(AgcBlit& b,uint32_t w,uint32_t h):impl_(std::make_unique<Impl>(b,w,h)){
    try{impl_->initialize();}catch(...){if(impl_->close()<0){std::fputs("[mkw-present] setup close failed; scan-out owners retained\n",stderr);(void)impl_.release();}throw;}
}
VideoPresenter::~VideoPresenter(){if(impl_&&impl_->close()<0){std::fputs("[mkw-present] close failed; scan-out owners retained\n",stderr);(void)impl_.release();}}
PresentedFrame VideoPresenter::present(std::shared_ptr<const GpuColorTarget> source,float aspect,bool linear,uint64_t deadline){
    (void)impl_->submit(std::move(source),aspect,linear,deadline);return impl_->retire();
}
PresentedFrame VideoPresenter::submit(std::shared_ptr<const GpuColorTarget> source,float aspect,bool linear,uint64_t deadline){return impl_->submit(std::move(source),aspect,linear,deadline);}
bool VideoPresenter::retire_pending(){if(!impl_->outstanding)return false;(void)impl_->retire();return true;}
const GpuColorTarget& VideoPresenter::current_target() const{
    if(impl_->current<0||impl_->failed||impl_->outstanding)throw std::logic_error("No retired presentation target");
    return *impl_->buffers[impl_->current];
}
int VideoPresenter::close() noexcept{return impl_->close();}
}
