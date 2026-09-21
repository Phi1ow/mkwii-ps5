// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "gx_viewport.h"
#include <optional>
namespace mkw::agc {
struct GxVideoSettings {
    std::optional<GXRenderModeObj> mode;
    AuroraViewportPolicy policy=AURORA_VIEWPORT_FIT;
    float scale=0;
    int aspectWidth=0,aspectHeight=0;
    uint32_t surfaceWidth=1920,surfaceHeight=1080;
};
struct GxVideoPlan {
    GxRenderExtent extent;
    float aspectCorrection=1,presentAspect=0; // zero: use the copied image aspect
    AuroraViewportPolicy policy;
};
GxVideoPlan plan_gx_video(const GxVideoSettings&);
GxVideoPlan snapshot_gx_video();
void commit_gx_video_size(std::optional<GxRenderExtent>);
}
