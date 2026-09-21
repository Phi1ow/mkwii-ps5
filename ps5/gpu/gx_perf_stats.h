// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <atomic>
#include <cstdint>
#include <initializer_list>
namespace mkw::agc {
// Frame-level accounting for the PS5 GX backend. Counters accumulate between
// the periodic report printed by GxFrameRenderer and are reset afterwards.
// Header-only so every test/diagnostic that links the backend keeps building.
struct GxPerfStats {
    std::atomic<uint64_t> drawCount{0},drawNanos{0},drawWaitNanos{0};
    // drawNanos split: packet build (GX state snapshot, materials, textures)
    // versus submission (upload, command buffer, driver call).
    std::atomic<uint64_t> buildNanos{0},submitNanos{0};
    // Whole FIFO draw receiver: vertex layout, geometry snapshot and the
    // renderer submission above. frontend minus drawNanos is decode cost.
    std::atomic<uint64_t> frontendNanos{0};
    // A clock read costs ~22 us on PS5 (perf_microbench), so per-draw phases
    // are timed on one draw in kDrawTimingStride and scaled by the exact count.
    std::atomic<uint64_t> timedDraws{0},timedFrontends{0},frontendCount{0};
    // Draws the frontend drops before any GPU work. A model that never
    // appears on screen shows up here rather than in a log line.
    std::atomic<uint64_t> skipEmptyCount{0},skipCullCount{0},skipNoIndexCount{0};
    // Primitive commands folded into the preceding draw of the same stream
    // (gx_draw.cpp); drawCount counts the submitted draws.
    std::atomic<uint64_t> mergedDrawCount{0};
    std::atomic<uint64_t> blitCount{0},blitNanos{0};
    // Time AgcBlit spends waiting for GPU completion (arena reuse, finish) and how often.
    std::atomic<uint64_t> blitWaitNanos{0},blitWaits{0};
    std::atomic<uint64_t> presentNanos{0},fifoNanos{0};
    // Sub-phases on the same sampled draws: vertex snapshot inside the
    // frontend; raster/transform/TEV state, geometry serialization and
    // material (textures) inside the packet build.
    std::atomic<uint64_t> snapshotNanos{0},stateNanos{0},serializeNanos{0},materialNanos{0};
    // Sampled like the phases above: vertex layout built per draw command.
    std::atomic<uint64_t> layoutNanos{0};
    // Exact volumes of the submitted geometry snapshots: primitive commands
    // (segments), vertices, and bytes copied from vertex records and arrays.
    std::atomic<uint64_t> segmentCount{0},snapshotVertices{0},snapshotRecordBytes{0},snapshotArrayBytes{0};
    // Every GXCopyTex: whole call, FIFO drain before it, draw batch finish,
    // and RAM publication (copy store, deferred read); blits are counted above.
    std::atomic<uint64_t> copyTexCount{0},copyTexNanos{0},copyDrainNanos{0},copyFinishNanos{0},copyPublishNanos{0};
    // Publication split: readback of partially overlapped older copies, the new
    // deferred registration, retirement of replaced copies; and every deferred
    // copy materialized into guest RAM (GPU wait, encode, store).
    std::atomic<uint64_t> copyOverlapNanos{0},copyDeferNanos{0},copyRetireNanos{0},materializeCount{0},materializeNanos{0};
    // GPU color targets created and destroyed (direct-memory allocation, mapping
    // and clear; unmapping and release), e.g. EFB copy targets outside the pool.
    std::atomic<uint64_t> targetCreates{0},targetCreateNanos{0},targetDestroys{0},targetDestroyNanos{0};
    // Inside every blit: driver submission and AGC suspend point; blits that
    // clear (constant source) and that copy depth.
    std::atomic<uint64_t> blitSubmitNanos{0},blitSuspendNanos{0},blitClears{0},blitDepthCopies{0};
    void reset() noexcept{
        for(auto* c:{&drawCount,&drawNanos,&drawWaitNanos,&buildNanos,&submitNanos,&frontendNanos,&timedDraws,&timedFrontends,&frontendCount,
                     &skipEmptyCount,&skipCullCount,&skipNoIndexCount,&mergedDrawCount,
                     &blitCount,&blitNanos,&blitWaitNanos,&blitWaits,&presentNanos,&fifoNanos,&snapshotNanos,&stateNanos,&serializeNanos,&materialNanos,
                     &layoutNanos,&segmentCount,&snapshotVertices,&snapshotRecordBytes,&snapshotArrayBytes,
                     &copyTexCount,&copyTexNanos,&copyDrainNanos,&copyFinishNanos,&copyPublishNanos,
                     &copyOverlapNanos,&copyDeferNanos,&copyRetireNanos,&materializeCount,&materializeNanos,
                     &targetCreates,&targetCreateNanos,&targetDestroys,&targetDestroyNanos,
                     &blitSubmitNanos,&blitSuspendNanos,&blitClears,&blitDepthCopies})c->store(0,std::memory_order_relaxed);
    }
};
inline GxPerfStats& gx_perf_stats(){static GxPerfStats stats;return stats;}
inline constexpr uint64_t kDrawTimingStride=64;
// Set by the sampled-draw timers on the producer thread for the duration of one draw.
inline thread_local bool g_timedFrontend=false,g_timedBuild=false;
// Host phase of the producer thread, read by the diagnostic sampler so time
// spent in native code (invisible to guest LR sampling) is still attributed.
enum class HostPhase : uint32_t { Guest=0, FrameBegin, FrameEnd, Present, InputPoll, FrameReport };
inline constexpr const char* kHostPhaseNames[]={"guest","frame-begin","frame-end","present","input-poll","frame-report"};
inline std::atomic<uint32_t> g_hostPhase{0};
struct HostPhaseScope {
    uint32_t previous;
    explicit HostPhaseScope(HostPhase phase) noexcept:previous(g_hostPhase.exchange(uint32_t(phase),std::memory_order_relaxed)){}
    ~HostPhaseScope(){g_hostPhase.store(previous,std::memory_order_relaxed);}
    HostPhaseScope(const HostPhaseScope&)=delete;HostPhaseScope& operator=(const HostPhaseScope&)=delete;
};
inline void gx_perf_add(std::atomic<uint64_t>& counter,uint64_t value){counter.fetch_add(value,std::memory_order_relaxed);}
}
