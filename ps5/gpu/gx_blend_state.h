// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "gx/register_state.hpp"
#include <array>
namespace mkw::agc {
struct GxBlendState {
    uint32_t control;
    uint32_t targetMask;
    std::array<float,4> constant;
};
// Target zero: CxBlendControl 0x1e0, target mask 0x8e, constants 0x105..108.
// Preserve the driver defaults outside SharpProspero's defined blend fields.
GxBlendState snapshot_gx_blend(const aurora::gx::GXRegisterState&,uint32_t defaultControl);
}
