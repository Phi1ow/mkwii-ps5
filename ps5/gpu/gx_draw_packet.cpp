// SPDX-License-Identifier: GPL-3.0-only
#include "gx_draw_packet.h"
#include "gx_perf_stats.h"
#include "gpu_wait_service.h"
#include <stdexcept>
namespace mkw::agc {
uint32_t snapshot_gx_depth_control(const aurora::gx::GXRegisterState& s){
    uint32_t compare=7;
    if(s.depthCompare){
        if(unsigned(s.depthFunc)>7)throw std::invalid_argument("Invalid GX depth comparison");
        // WiiCompiled gx.cpp::to_compare_function and SharpProspero
        // CxDepthStencilControl.CompareFunction use these semantic operations.
        constexpr uint32_t reversed[8]={0,4,2,6,1,5,3,7};
        compare=aurora::gx::UseReversedZ?reversed[unsigned(s.depthFunc)]:unsigned(s.depthFunc);
    }
    return 2u|(s.depthUpdate?4u:0u)|(compare<<4);
}
namespace {
// Validation and snapshot shared by both build overloads; leaves indices empty.
GxDrawPacket build_packet(const GxGeometry& geometry,const aurora::gx::GXRegisterState& s,
    GxTextureCache& cache,GxSourceResolver resolver,void* context,uint16_t anisotropy){
    if(geometry.primitive()==GX_LINES||geometry.primitive()==GX_LINESTRIP||geometry.primitive()==GX_POINTS)
        throw std::invalid_argument("GX line/point expansion is not implemented by the triangle shader");
    if(geometry.indices().empty()||geometry.indices().size()%3)
        throw std::invalid_argument("GX triangle draw has no complete primitives");
    if(unsigned(s.cullMode)>3)throw std::invalid_argument("Invalid GX cull mode");
    if(s.zTextureOp!=GX_ZT_DISABLE)throw std::invalid_argument("GX depth texture requires shader depth output");
    // GX_ZCOMPLOC (depth test before texturing) is accepted like the reference
    // aurora WebGPU pipeline: it only changes results when the Z texture is
    // written late, and that path is rejected above. With an alpha test the
    // Wii still writes depth for early-rejected pixels; this pipeline, like the
    // reference, lets the fragment kill skip the depth write instead.
    const bool timed=g_timedBuild;
    uint64_t mark=timed?gpu_clock_nanos():0;
    const auto lap=[&](std::atomic<uint64_t>& counter){if(timed){const auto now=gpu_clock_nanos();gx_perf_add(counter,now-mark);mark=now;}};
    GxDrawPacket p;
    p.depthControl=snapshot_gx_depth_control(s);p.cullMode=unsigned(s.cullMode);
    p.viewport={s.renderViewport,s.renderScissor};
    (void)snapshot_gx_viewport(p.viewport); // Reject invalid raster state before uploads.
    p.blend=snapshot_gx_blend(s,0);
    p.transforms=GxTransformState::snapshot(s);p.lighting=snapshot_gx_lighting(s);
    // Validate TEV dependencies and texgen before allocating material textures.
    auto program=mkw::gpu::build_tev_program(s);
    for(unsigned i=0;i<program.stage_count;++i){const auto& stage=program.stages[i];
        if(stage.sample_enabled&&stage.tex_coord>=8)throw std::invalid_argument("Invalid GX texture coordinate");
    }
    p.texgen=snapshot_gx_texgen(s,mkw::gpu::tev_program_used_texcoords(s));
    lap(gx_perf_stats().stateNanos);
    p.geometry=serialize_gx_geometry(geometry);
    lap(gx_perf_stats().serializeNanos);
    p.material=GxDirectMaterial::build(program,s,cache,resolver,context,anisotropy);
    lap(gx_perf_stats().materialNanos);
    return p;
}
}
GxDrawPacket GxDrawPacket::build(const GxGeometry& geometry,const aurora::gx::GXRegisterState& s,
    GxTextureCache& cache,GxSourceResolver resolver,void* context,uint16_t anisotropy){
    auto p=build_packet(geometry,s,cache,resolver,context,anisotropy);
    p.indices=geometry.indices();
    return p;
}
GxDrawPacket GxDrawPacket::build(GxGeometry&& geometry,const aurora::gx::GXRegisterState& s,
    GxTextureCache& cache,GxSourceResolver resolver,void* context,uint16_t anisotropy){
    auto p=build_packet(geometry,s,cache,resolver,context,anisotropy);
    p.indices=geometry.take_indices();  // validated above against these same indices
    return p;
}
}
