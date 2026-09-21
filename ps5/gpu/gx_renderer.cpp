// SPDX-License-Identifier: GPL-3.0-only
#include "gx_renderer.h"
#include "gx_perf_stats.h"
#include "gpu_wait_service.h"
#include <stdexcept>
namespace mkw::agc {
GxRenderer::GxRenderer(void* vs,void* ps,std::shared_ptr<GpuColorTarget> color,std::shared_ptr<GpuDepthTarget> depth,
    GxArrayResolver arrays,GxSourceResolver source,void* context,uint16_t anisotropy):draw_(vs,ps),color_(std::move(color)),depth_(std::move(depth)),
    arrays_(arrays),source_(source),context_(context),anisotropy_(anisotropy){
    if(!color_||!depth_||!color_->data()||!depth_->data()||color_->layout().width()!=depth_->layout().width()||
        color_->layout().height()!=depth_->layout().height()||!arrays_||!source_||
        (anisotropy!=1&&anisotropy!=2&&anisotropy!=4&&anisotropy!=8&&anisotropy!=16))throw std::invalid_argument("Invalid GX renderer binding");
}
GxDrawSink GxRenderer::sink() noexcept{
    return {+[](void* p,GXAttr a,const aurora::gx::AttrArray& array,uint32_t offset,uint32_t bytes){
        auto& r=*static_cast<GxRenderer*>(p);return r.arrays_(r.context_,a,array,offset,bytes);},
        +[](void* p,GxGeometry&& g,const aurora::gx::GXRegisterState& s){static_cast<GxRenderer*>(p)->submit(std::move(g),s);},this};
}
void GxRenderer::submit(GxGeometry&& geometry,const aurora::gx::GXRegisterState& state){
    // Draws pipeline on the GPU queue; AgcGxDraw waits only when a slot is reused.
    auto& stats=gx_perf_stats();
    const bool timed=stats.drawCount.fetch_add(1,std::memory_order_relaxed)%kDrawTimingStride==0;
    const auto started=timed?gpu_clock_nanos():0;
    g_timedBuild=timed;
    auto packet=GxDrawPacket::build(std::move(geometry),state,textures_,source_,context_,anisotropy_);
    g_timedBuild=false;
    const auto built=timed?gpu_clock_nanos():0;
    completed_=draw_.draw(std::move(packet),color_,depth_);
    if(timed){
        const auto done=gpu_clock_nanos();
        gx_perf_add(stats.buildNanos,built-started);gx_perf_add(stats.submitNanos,done-built);
        gx_perf_add(stats.drawNanos,done-started);gx_perf_add(stats.timedDraws,1);
    }
}
void GxRenderer::finish() const{draw_.finish();}
void GxRenderer::submit_draws() const{draw_.submit();}
void GxRenderer::clear_texture_cache(){finish();textures_.clear();}
}
