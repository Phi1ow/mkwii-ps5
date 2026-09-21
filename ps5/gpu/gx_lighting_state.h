// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "gx/register_state.hpp"
#include "gx_lighting.h"
namespace mkw::agc {
MkwLighting snapshot_gx_lighting(const aurora::gx::GXRegisterState&);
}
static_assert(sizeof(MkwLight)==80 && sizeof(MkwLightChannel)==64 && sizeof(MkwLighting)==MKW_GX_LIGHTING_BYTES);
