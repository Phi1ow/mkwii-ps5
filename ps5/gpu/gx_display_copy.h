// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "gx_texture_copy.h"
#include <vector>
namespace mkw::agc {
// Display copies use the supplied WiiCompiled's GPU present source, not an
// encoded XFB in guest RAM. A presenter keeps the returned owner until retired.
// All methods require exclusion of concurrent guest/render/presenter mutation.
GxTextureCopyPlan plan_gx_display_copy(const aurora::gx::GXRegisterState&,GxRenderExtent,bool clear,bool disableFilter=false);
class GxDisplayCopyBackend {
public:
    GxDisplayCopyBackend(AgcBlit&,std::shared_ptr<GpuColorTarget>,std::shared_ptr<GpuDepthTarget>,
        GxRenderExtent,GxTextureCopyBackend::FinishDraws,void*);
    std::shared_ptr<const GpuColorTarget> execute(const aurora::gx::GXRegisterState&,bool clear);
    std::shared_ptr<const GpuColorTarget> latest() const noexcept{return latest_;}
    void begin_frame() noexcept{latest_.reset();}
    void set_disable_filter(bool value) noexcept{disableFilter_=value;}
private:
    AgcBlit& blitter_;
    std::shared_ptr<GpuColorTarget> source_;
    std::shared_ptr<GpuDepthTarget> depth_;
    GxRenderExtent extent_;
    GxTextureCopyBackend::FinishDraws finish_;
    void* user_;
    bool disableFilter_=false;
    std::vector<std::shared_ptr<GpuColorTarget>> pool_;
    std::shared_ptr<const GpuColorTarget> latest_;
};
// Guest execution must be excluded during replacement/destruction. Returns
// the previous receiver so a scoped renderer can restore its caller's binding.
GxDisplayCopyBackend* exchange_gx_display_copy_backend(GxDisplayCopyBackend*);
}
