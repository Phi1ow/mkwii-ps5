// SPDX-License-Identifier: GPL-3.0-only
#include "gx_texture_copy.h"
#include "gx/fifo.hpp"
#include "gx/register_backend.hpp"
#include "gfx/efb_ram_encoder.hpp"
#include "memory.h"
#include "gx_guest_write.h"
#include "gx_perf_stats.h"
#include "gpu_wait_service.h"
#include <mutex>
#include <stdexcept>
namespace mkw::agc {
namespace {GxTextureCopyBackend* active=nullptr;}
GxTextureCopyBackend::GxTextureCopyBackend(AgcBlit& blit,std::shared_ptr<GpuColorTarget> source,GxRenderExtent extent,
    FinishDraws finish,void* user,std::shared_ptr<GpuDepthTarget> depth):blitter_(blit),source_(std::move(source)),depth_(std::move(depth)),extent_(extent),finish_(finish),user_(user){
    if(!source_||!source_->data()||!finish_||source_->layout().width()!=extent.targetWidth||
       source_->layout().height()!=extent.targetHeight)throw std::invalid_argument("Invalid GX EFB copy binding");
    if(depth_&&(!depth_->data()||depth_->layout().width()!=extent.targetWidth||depth_->layout().height()!=extent.targetHeight))
        throw std::invalid_argument("Invalid GX EFB depth binding");
}
std::shared_ptr<GpuColorTarget> GxTextureCopyBackend::pooled_target(uint32_t width,uint32_t height){
    // Bounded by direct memory, not by count: a copy to a destination is created
    // before the previous copy there is retired, so steady state keeps two
    // targets per copy (race: ~40 for 10 copies with scaled and native targets).
    // A 32-entry bound recreated and released 20 targets per race frame (~1 ms).
    constexpr size_t kPoolBytes=size_t(256)<<20;
    for(const auto& candidate:pool_)
        if(candidate.use_count()==1&&candidate->layout().width()==width&&candidate->layout().height()==height)return candidate;
    auto created=std::make_shared<GpuColorTarget>(width,height);
    if(poolBytes_+created->allocation_size()>kPoolBytes){
        // Replace an unreferenced target (another size); otherwise do not pool.
        for(auto& candidate:pool_)if(candidate.use_count()==1){
            poolBytes_=poolBytes_-candidate->allocation_size()+created->allocation_size();
            candidate=created;return created;
        }
        return created;
    }
    pool_.push_back(created);
    poolBytes_+=created->allocation_size();
    return created;
}
ColorCopyHandle GxTextureCopyBackend::execute(void* destination,const aurora::gx::GXRegisterState& state,bool clear){
    const auto plan=plan_gx_texture_copy(state,extent_,clear);
    // Fail before the synchronizer, destination alpha or any copy modifies GPU state.
    if(plan.clearDepth&&(!depth_||!depth_->data()))throw std::logic_error("GX depth clear requires the active EFB depth target");
    const size_t bytes=aurora::gfx::efb_ram::encoded_size(state.texCopyFmt,plan.logicalWidth,plan.logicalHeight);
    uint32_t guest=0;bool found=false;
    for(const auto bank:{std::pair{Memory::kMem1CachedBase,Memory::kMem1Size},std::pair{Memory::kMem2CachedBase,Memory::kMem2Size}}){
        if(Memory::Contains(bank.first,bank.second)&&GxGuestWrite::HostRangeToGuest(Memory::GetPointer(bank.first,bank.second),
           bank.second,bank.first,destination,bytes,guest)){found=true;break;}
    }
    if(!found)throw std::invalid_argument("GXCopyTex destination is not Wii RAM");
    auto& stats=gx_perf_stats();
    const auto finishStarted=gpu_clock_nanos();
    finish_(user_);
    gx_perf_add(stats.copyFinishNanos,gpu_clock_nanos()-finishStarted);
    if(plan.preCopyAlpha!=UINT32_MAX)
        blitter_.clear_color(source_,{0,0,extent_.targetWidth,extent_.targetHeight},plan.preCopyAlpha<<24,8);
    auto scaled=pooled_target(plan.scaledWidth,plan.scaledHeight);
    if(plan.depth){
        if(!depth_||!depth_->data())throw std::logic_error("GX depth copy requires the active EFB depth target");
        blitter_.copy_depth(depth_,scaled,plan.source,{0,0,plan.scaledWidth,plan.scaledHeight},&plan.options);
    }else{
        blitter_.copy_sampled(source_,scaled,plan.source,{0,0,plan.scaledWidth,plan.scaledHeight},&plan.options);
    }
    std::shared_ptr<GpuColorTarget> native;
    if(plan.scaledWidth!=plan.logicalWidth||plan.scaledHeight!=plan.logicalHeight){
        native=pooled_target(plan.logicalWidth,plan.logicalHeight);
        // Depth bytes pack 24-bit fields: Aurora's RAM readback samples them
        // nearest, bilinear filtering would blend adjacent packed depths.
        GxCopyOptions downscale;downscale.linear=!plan.depth;
        blitter_.copy(scaled,native,{0,0,plan.scaledWidth,plan.scaledHeight},{0,0,plan.logicalWidth,plan.logicalHeight},&downscale);
    }
    if(plan.clearDepth)blitter_.fill_depth_tested(source_,depth_,plan.clearRect,plan.clearColor,
        {plan.depthClearValue,DepthCompare::Always,true},plan.clearMask);
    else if(plan.clearMask)blitter_.clear_color(source_,plan.clearRect,plan.clearColor,plan.clearMask);
    auto copy=std::make_shared<const GxColorCopy>(GxColorCopy{plan.logicalWidth,plan.logicalHeight,scaled,state.texCopyFmt,native});
    const auto publishStarted=gpu_clock_nanos();
    gx_copy_store().publish(guest,copy);
    gx_perf_add(stats.copyPublishNanos,gpu_clock_nanos()-publishStarted);
    return copy;
}
GxTextureCopyBackend* exchange_gx_texture_copy_backend(GxTextureCopyBackend* value){std::lock_guard lock(aurora::renderer_gpu_mutex());auto* old=active;active=value;return old;}
void set_gx_texture_copy_backend(GxTextureCopyBackend* value){(void)exchange_gx_texture_copy_backend(value);}
void execute_gx_texture_copy(void* destination,bool clear){
    auto& stats=gx_perf_stats();
    const auto started=gpu_clock_nanos();
    struct Total{uint64_t started;~Total(){auto& s=gx_perf_stats();gx_perf_add(s.copyTexNanos,gpu_clock_nanos()-started);gx_perf_add(s.copyTexCount,1);}} total{started};
    if(aurora::gx::fifo::get_buffer_size())aurora::gx::fifo::drain();
    gx_perf_add(stats.copyDrainNanos,gpu_clock_nanos()-started);
    std::lock_guard lock(aurora::renderer_gpu_mutex());
    if(!active)throw std::logic_error("GXCopyTex requires the active EFB renderer");
    active->execute(destination,aurora::gx::register_state(),clear);
}
}
extern "C" void GXCopyTex(void* destination,GXBool clear){mkw::agc::execute_gx_texture_copy(destination,clear!=GX_FALSE);}
