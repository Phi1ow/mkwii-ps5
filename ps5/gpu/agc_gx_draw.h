// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "gx_draw_packet.h"
#include "color_target.h"
#include "depth_target.h"
#include <memory>
namespace mkw::agc {
// Batched submission of the general raw-GX vertex + direct-TEV shader pair.
// Both must implement ten interpolated parameters (8 STQ, 2 rasters). AGC must
// be initialized; shader storage stays caller-owned through errors. Targets
// must be idle and color must not be displayed while drawing.
// draw() records into an open batch; a batch is submitted as one DCB with one
// completion serial and one suspend point when it holds BatchDraws draws, runs
// out of space, or finish() is called, as the SDK documents for a frame. Any
// caller that reads a target on the GPU or CPU must call finish() first.
// Up to InFlightSlots batches may be executing while the next one records. A
// failed submission or timeout keeps every resource and refuses further work.
class AgcGxDraw {
public:
    static constexpr unsigned InFlightSlots=4;
    static constexpr unsigned BatchDraws=256;
    AgcGxDraw(void* preparedVertexShader,void* preparedPixelShader);
    ~AgcGxDraw();
    AgcGxDraw(const AgcGxDraw&)=delete;
    AgcGxDraw& operator=(const AgcGxDraw&)=delete;
    uint64_t draw(GxDrawPacket,std::shared_ptr<GpuColorTarget>,std::shared_ptr<GpuDepthTarget>);
    // Submits the open batch, waits (bounded) for all submitted work and releases owners.
    void finish() const;
    // Submits the open batch without waiting. Later AGC submissions (EFB copy
    // blits) run after it on the GPU queue; CPU readers still use finish().
    void submit() const;
    // True while submitted work is unresolved or after a failure; an open batch is not pending.
    bool pending() const noexcept;
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
}
