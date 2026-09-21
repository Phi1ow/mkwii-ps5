// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "agc_blit.h"
namespace mkw::agc {
BlitRect fit_present_rect(uint32_t width,uint32_t height,float aspect);
struct PresentedFrame {uint64_t serial,count,time;int buffer;BlitRect viewport;};
// Single producer, exclusive VideoOut handle. The blitter and its shaders
// must outlive this object. Copies to private scan-out buffers on the GPU;
// source owners may be released once present returns. Each flip is retired
// before returning, but its current scan-out buffer remains protected until
// a later flip displays the other buffer or VideoOutClose detaches both.
class VideoPresenter {
public:
    VideoPresenter(AgcBlit&,uint32_t width=1920,uint32_t height=1080);
    ~VideoPresenter();
    VideoPresenter(const VideoPresenter&)=delete;
    VideoPresenter& operator=(const VideoPresenter&)=delete;
    PresentedFrame present(std::shared_ptr<const GpuColorTarget>,float aspect,bool linear=true,uint64_t deadlineNanos=0);
    // Same copy and flip, returning once the flip is submitted instead of on
    // screen: the caller does not wait for the vertical blank. The flip is
    // retired at the next submit/present or by retire_pending(); until then
    // current_target() refuses. count and time describe the last retired flip.
    PresentedFrame submit(std::shared_ptr<const GpuColorTarget>,float aspect,bool linear=true,uint64_t deadlineNanos=0);
    // Waits for a submitted flip to reach the screen; false when none is outstanding.
    bool retire_pending();
    // Diagnostic read-only borrow: valid until the next present or close.
    const GpuColorTarget& current_target() const;
    int close() noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
