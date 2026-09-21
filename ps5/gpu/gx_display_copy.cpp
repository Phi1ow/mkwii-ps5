// SPDX-License-Identifier: GPL-3.0-only
#include <algorithm>
#include "gx_display_copy.h"
#include "gx/fifo.hpp"
#include "gx/register_backend.hpp"
#include <mutex>
#include <stdexcept>
namespace mkw::agc {
namespace {GxDisplayCopyBackend* active=nullptr;}
GxDisplayCopyBackend::GxDisplayCopyBackend(AgcBlit& blit,std::shared_ptr<GpuColorTarget> source,
    std::shared_ptr<GpuDepthTarget> depth,GxRenderExtent extent,GxTextureCopyBackend::FinishDraws finish,void* user)
    :blitter_(blit),source_(std::move(source)),depth_(std::move(depth)),extent_(extent),finish_(finish),user_(user){
    if(!source_||!source_->data()||!finish_||source_->layout().width()!=extent.targetWidth||source_->layout().height()!=extent.targetHeight)
        throw std::invalid_argument("Invalid display-copy EFB binding");
    if(depth_&&(!depth_->data()||depth_->layout().width()!=extent.targetWidth||depth_->layout().height()!=extent.targetHeight))
        throw std::invalid_argument("Invalid display-copy depth binding");
}
std::shared_ptr<const GpuColorTarget> GxDisplayCopyBackend::execute(const aurora::gx::GXRegisterState& s,bool clear){
    const auto p=plan_gx_display_copy(s,extent_,clear,disableFilter_);
    if(p.clearDepth&&(!depth_||!depth_->data()))throw std::logic_error("Display-copy clear requires an EFB depth target");
    finish_(user_);
    std::shared_ptr<GpuColorTarget> target;
    // Asynchronous blits keep their destinations referenced until they complete.
    if(pool_.size()>=3&&std::none_of(pool_.begin(),pool_.end(),[](const auto& c){return c.use_count()==1;}))blitter_.finish();
    for(auto& candidate:pool_)if(candidate.use_count()==1){
        if(candidate->layout().width()!=p.scaledWidth||candidate->layout().height()!=p.scaledHeight)
            candidate=std::make_shared<GpuColorTarget>(p.scaledWidth,p.scaledHeight);
        target=candidate;break;
    }
    if(!target){
        if(pool_.size()>=3)throw std::logic_error("Display-copy presenter has not retired previous buffers");
        target=std::make_shared<GpuColorTarget>(p.scaledWidth,p.scaledHeight);pool_.push_back(target);
    }
    blitter_.copy_sampled(source_,target,p.source,{0,0,p.scaledWidth,p.scaledHeight},&p.options);
    if(p.clearDepth)blitter_.fill_depth_tested(source_,depth_,p.clearRect,p.clearColor,
        {p.depthClearValue,DepthCompare::Always,true},p.clearMask);
    else if(p.clearMask)blitter_.clear_color(source_,p.clearRect,p.clearColor,p.clearMask);
    // Commit only after both copy and clear complete; failures retain the last
    // presentable frame and AgcBlit's pending GPU resources.
    latest_=std::move(target);return latest_;
}
GxDisplayCopyBackend* exchange_gx_display_copy_backend(GxDisplayCopyBackend* value){
    std::lock_guard lock(aurora::renderer_gpu_mutex());auto* old=active;active=value;return old;
}
void execute_gx_display_copy(bool clear){
    if(aurora::gx::fifo::get_buffer_size())aurora::gx::fifo::drain();
    std::lock_guard lock(aurora::renderer_gpu_mutex());
    if(!active)throw std::logic_error("GXCopyDisp requires an active display-copy receiver");
    active->execute(aurora::gx::register_state(),clear);
}
}
extern "C" void GXCopyDisp(void* destination,GXBool clear){
    (void)destination; // Supplied WiiCompiled also keeps its XFB on the GPU.
    mkw::agc::execute_gx_display_copy(clear!=GX_FALSE);
}
