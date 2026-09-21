// SPDX-License-Identifier: GPL-3.0-only
#include "gx_draw_backend.h"
#include "gx/register_backend.hpp"
#include <mutex>
#include <stdexcept>
namespace mkw::agc {
namespace {GxDrawSink active;}
GxDrawSink exchange_gx_draw_sink(GxDrawSink sink){
    if(bool(sink.resolve)!=bool(sink.submit))throw std::invalid_argument("Incomplete GX draw receiver");
    std::lock_guard lock(aurora::renderer_gpu_mutex());auto old=active;active=sink;return old;
}
std::span<const uint8_t> resolve_gx_array(GXAttr attr,const aurora::gx::AttrArray& array,uint32_t offset,uint32_t bytes){
    if(!active.resolve)throw std::logic_error("GX arrays require the active renderer");
    if(!array.data||offset>array.size||bytes>array.size-offset)throw std::out_of_range("GX array exceeds declared interval");
    auto range=active.resolve(active.context,attr,array,offset,bytes);
    if(range.size()<bytes)throw std::out_of_range("GX array resolver returned truncated storage");
    return range.first(bytes);
}
void submit_gx_geometry(GxGeometry&& geometry,const aurora::gx::GXRegisterState& state){
    if(!active.submit)throw std::logic_error("GX draws require the active renderer");
    active.submit(active.context,std::move(geometry),state);
}
}
