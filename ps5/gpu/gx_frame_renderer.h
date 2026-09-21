// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "gx_video_state.h"
#include "video_presenter.h"
namespace mkw::agc {
struct GxPreparedShaders {void* vertex;void* pixel;void* copyVertex;void* copyPixel;};
// Owns the EFB, drawing/copy receivers and scan-out. Prepared shader storage
// must remain alive. Single producer; configuration changes are committed at
// begin_frame. Exceptions require closing the failed renderer before reuse.
class GxFrameRenderer {
public:
    explicit GxFrameRenderer(GxPreparedShaders,uint16_t anisotropy=1);
    ~GxFrameRenderer();
    GxFrameRenderer(const GxFrameRenderer&)=delete;
    GxFrameRenderer& operator=(const GxFrameRenderer&)=delete;
    bool begin_frame();
    void end_frame();
    void wait_idle() const;
    void set_disable_copy_filter(bool);
    PresentedFrame last_presented() const;
    const GpuColorTarget& current_target() const;
    GxVideoPlan current_video_plan() const;
    // Zero proves normal retirement/close; a failed frame retains resources
    // and returns negative even after detaching VideoOut. Keep shader owners.
    int close();
private:struct Impl;std::unique_ptr<Impl> impl_;
};
// The game bootstrap installs one owner before its first Aurora begin_frame.
// Does not transfer ownership. Uninstalled calls report a missing backend.
GxFrameRenderer* exchange_gx_frame_renderer(GxFrameRenderer*);
GxFrameRenderer* current_gx_frame_renderer();
}
