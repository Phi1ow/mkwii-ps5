// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "gx_geometry.h"
namespace mkw::agc {
struct GxDrawSink {
    GxArrayResolver resolve=nullptr;
    void (*submit)(void*,GxGeometry&&,const aurora::gx::GXRegisterState&)=nullptr;
    void* context=nullptr;
};
// Change only while guest execution is excluded. The receiver and callback
// context remain caller-owned. Returns the previous receiver for scoped use.
GxDrawSink exchange_gx_draw_sink(GxDrawSink);
class ScopedGxDrawSink {
public:
    explicit ScopedGxDrawSink(GxDrawSink sink):previous_(exchange_gx_draw_sink(sink)){}
    ~ScopedGxDrawSink(){exchange_gx_draw_sink(previous_);}
    ScopedGxDrawSink(const ScopedGxDrawSink&)=delete;
    ScopedGxDrawSink& operator=(const ScopedGxDrawSink&)=delete;
private:GxDrawSink previous_;
};
// AGC renderer entry points. Missing installation is an explicit error.
// Both run under renderer_gpu_mutex. Array resolution must validate the actual
// mapping. Submission must snapshot matrices, material and other GX state
// before returning; it may not retain references to mutable register state.
std::span<const uint8_t> resolve_gx_array(GXAttr,const aurora::gx::AttrArray&,uint32_t offset,uint32_t bytes);
void submit_gx_geometry(GxGeometry&&,const aurora::gx::GXRegisterState&);
}
