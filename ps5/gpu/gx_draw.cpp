// SPDX-License-Identifier: GPL-3.0-only
#include "gx_draw_backend.h"
#include "gx_perf_stats.h"
#include "gpu_wait_service.h"
#include "gx/register_backend.hpp"
#include "gx/command_processor.hpp"
#include "dolphin/gx/frontend.hpp"
#include "dolphin/gx/__gx.h"
#include <mutex>
#include <optional>
#include <vector>

namespace aurora::gx::fifo {
namespace {
std::span<const uint8_t> resolve_array(void*,GXAttr attr,const AttrArray& array,uint32_t offset,uint32_t bytes) {
    return mkw::agc::resolve_gx_array(attr,array,offset,bytes);
}
void count_snapshot(const mkw::agc::GxGeometry& packet,size_t segments) {
    auto& s=mkw::agc::gx_perf_stats();
    size_t arrayBytes=0;for(const auto& array:packet.arrays())arrayBytes+=array.bytes.size();
    mkw::agc::gx_perf_add(s.segmentCount,segments);mkw::agc::gx_perf_add(s.snapshotVertices,packet.vertex_count());
    mkw::agc::gx_perf_add(s.snapshotRecordBytes,packet.records().size());mkw::agc::gx_perf_add(s.snapshotArrayBytes,arrayBytes);
}
void submit(const mkw::agc::GxVertexLayout& layout,GXPrimitive primitive,uint16_t count,std::span<const uint8_t> records) {
    const auto& state=register_state();
    if(count==0){mkw::agc::gx_perf_add(mkw::agc::gx_perf_stats().skipEmptyCount,1);return;}
    if(state.cullMode==GX_CULL_ALL && primitive!=GX_LINES && primitive!=GX_LINESTRIP && primitive!=GX_POINTS){
        mkw::agc::gx_perf_add(mkw::agc::gx_perf_stats().skipCullCount,1);return;}
    const bool timed=mkw::agc::g_timedFrontend;
    const auto started=timed?mkw::agc::gpu_clock_nanos():0;
    auto packet=mkw::agc::GxGeometry::snapshot(layout,state,primitive,count,records,resolve_array,nullptr);
    if(timed)mkw::agc::gx_perf_add(mkw::agc::gx_perf_stats().snapshotNanos,mkw::agc::gpu_clock_nanos()-started);
    // No complete triangle: lines/points and truncated batches land here,
    // which is how a model can silently fail to appear.
    if(packet.indices().empty()){mkw::agc::gx_perf_add(mkw::agc::gx_perf_stats().skipNoIndexCount,1);return;}
    count_snapshot(packet,1);
    mkw::agc::submit_gx_geometry(std::move(packet),state);
    // stateDirty remains set until a real renderer has resolved its pipeline.
}
bool triangle_class(GXPrimitive primitive) {
    return primitive==GX_QUADS||primitive==GX_TRIANGLES||primitive==GX_TRIANGLESTRIP||primitive==GX_TRIANGLEFAN;
}
// Consecutive triangle draw commands of one command stream. Only draws run
// between them, so the register state and the vertex layout are the ones the
// first saw; they are submitted as a single geometry. Guest thread only.
struct PendingDraws {
    std::vector<uint8_t> records;
    std::vector<mkw::agc::GxDrawSegment> segments;
    mkw::agc::GxVertexLayout layout;
    GXVtxFmt format=GX_VTXFMT0;
    uint32_t vertices=0;
};
PendingDraws& pending_draws() {static PendingDraws pending;return pending;}
// Bounds one GPU draw's upload; far below the draw batch arena.
constexpr size_t kMergedRecordBytes=size_t(1)<<18;
void submit_pending(PendingDraws& pending) {
    struct Clear{PendingDraws& p;~Clear(){p.records.clear();p.segments.clear();p.vertices=0;}} clear{pending};
    if(pending.segments.empty())return;
    if(pending.segments.size()==1){
        submit(pending.layout,pending.segments[0].primitive,pending.segments[0].count,pending.records);
        return;
    }
    const auto& state=register_state();
    auto& stats=mkw::agc::gx_perf_stats();
    mkw::agc::gx_perf_add(stats.mergedDrawCount,pending.segments.size()-1);
    if(state.cullMode==GX_CULL_ALL){mkw::agc::gx_perf_add(stats.skipCullCount,1);return;}
    const bool timed=mkw::agc::g_timedFrontend;
    const auto started=timed?mkw::agc::gpu_clock_nanos():0;
    auto packet=mkw::agc::GxGeometry::snapshot_segments(pending.layout,state,pending.segments,pending.records,resolve_array,nullptr);
    if(timed)mkw::agc::gx_perf_add(stats.snapshotNanos,mkw::agc::gpu_clock_nanos()-started);
    if(packet.indices().empty()){mkw::agc::gx_perf_add(stats.skipNoIndexCount,1);return;}
    count_snapshot(packet,pending.segments.size());
    mkw::agc::submit_gx_geometry(std::move(packet),state);
}
// True when a complete triangle draw with this vertex format starts at `at`
// (after GX_NOP padding) inside the same stream.
bool next_draw_joins(const u8* data,u32 at,u32 size,bool bigEndian,GXVtxFmt format,u32 stride) {
    while(at<size&&data[at]==0)++at;
    if(at>=size||size-at<3)return false;
    const u8 command=data[at];
    const u8 opcode=command&0xf8u;
    if(opcode<0x80||opcode>0xa0||GXVtxFmt(command&7u)!=format)return false;
    const uint16_t count=bigEndian?(uint16_t(data[at+1])<<8)|data[at+2]:uint16_t(data[at+1])|(uint16_t(data[at+2])<<8);
    return uint64_t(count)*stride<=size-at-3;
}
}
bool handle_draw(u8 command,const u8* data,u32& pos,u32 size,bool bigEndian) {
    struct FrontendTimer{
        bool timed=mkw::agc::gx_perf_stats().frontendCount.fetch_add(1,std::memory_order_relaxed)%mkw::agc::kDrawTimingStride==0;
        uint64_t started=timed?mkw::agc::gpu_clock_nanos():0;
        FrontendTimer(){mkw::agc::g_timedFrontend=timed;}
        ~FrontendTimer(){mkw::agc::g_timedFrontend=false;if(timed){auto& s=mkw::agc::gx_perf_stats();
            mkw::agc::gx_perf_add(s.frontendNanos,mkw::agc::gpu_clock_nanos()-started);mkw::agc::gx_perf_add(s.timedFrontends,1);}}} timer;
    // register_dispatch holds the renderer mutex. Failure consumes nothing,
    // so the caller cannot continue from the middle of a truncated draw.
    if(!data || pos>size || size-pos<2)return false;
    auto primitive=mkw::agc::gx_primitive_from_command(command);
    auto format=GXVtxFmt(command&7u);
    uint16_t count=bigEndian?(uint16_t(data[pos])<<8)|data[pos+1]:uint16_t(data[pos])|(uint16_t(data[pos+1])<<8);
    auto& pending=pending_draws();
    // A pending group exists only when this command was checked by
    // next_draw_joins: same stream, same vertex format, nothing but NOP padding
    // since the group's first draw, so its layout is this draw's layout (and the
    // one the merged snapshot uses). Build it only for the first draw of a group.
    const bool joined=!pending.segments.empty()&&triangle_class(primitive)&&pending.format==format;
    const auto layoutStarted=timer.timed?mkw::agc::gpu_clock_nanos():0;
    // Not a plain local: default-initializing all attribute layouts per joined
    // draw cost as much as the build it avoids.
    std::optional<mkw::agc::GxVertexLayout> built;
    if(!joined)built.emplace(mkw::agc::GxVertexLayout::build(register_state(),format));
    const mkw::agc::GxVertexLayout& layout=joined?pending.layout:*built;
    if(timer.timed)mkw::agc::gx_perf_add(mkw::agc::gx_perf_stats().layoutNanos,mkw::agc::gpu_clock_nanos()-layoutStarted);
    const uint64_t total=uint64_t(count)*layout.stride;
    if(total>size-pos-2)return false;
    if(!triangle_class(primitive)){
        submit(layout,primitive,count,{data+pos+2,size_t(total)});
        pos+=2+uint32_t(total);return true;
    }
    // A pending group only survives a call when the next command of this
    // stream is a complete compatible draw, so it is always this draw's
    // predecessor under the same state and layout.
    try{
        if(pending.segments.empty()){pending.layout=layout;pending.format=format;}
        pending.records.insert(pending.records.end(),data+pos+2,data+pos+2+total);
        pending.segments.push_back({primitive,count});
        pending.vertices+=count;
        pos+=2+uint32_t(total);
        if(pending.records.size()<kMergedRecordBytes&&pending.vertices<=std::numeric_limits<uint16_t>::max()*64u&&
           next_draw_joins(data,pos,size,bigEndian,format,layout.stride))
            return true;
    }catch(...){pending.records.clear();pending.segments.clear();pending.vertices=0;throw;}
    submit_pending(pending);
    return true;
}
bool submit_raw_draw(GXPrimitive primitive,GXVtxFmt format,const uint8_t* vertices,uint16_t count,uint32_t bytes) {
    if(!vertices || !count || !bytes)return false;
    if(__gx->dirtyState)__GXSetDirtyState();
    drain();
    std::lock_guard lock(aurora::renderer_gpu_mutex());
    auto layout=mkw::agc::GxVertexLayout::build(register_state(),format);
    if(uint64_t(count)*layout.stride!=bytes)return false;
    submit(layout,primitive,count,{vertices,bytes});return true;
}
}
