// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "gx/register_state.hpp"
#include "gx_transform_format.h"
namespace mkw::agc {
// Owns a draw's matrix values. Build while holding the GX state lock, then
// retain the upload through retirement. No matrix compaction or interpolation.
struct GxTransformState {
    std::array<float,MKW_GX_TRANSFORM_BYTES/4> values{};
    static GxTransformState snapshot(const aurora::gx::GXRegisterState&);
};
static_assert(sizeof(GxTransformState)==MKW_GX_TRANSFORM_BYTES);
}
