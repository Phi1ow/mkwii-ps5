// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "gx/register_state.hpp"
#include "gx_fog.h"
namespace mkw::agc {
// Fog snapshot for the direct-TEV fragment shader. Fog types outside the
// reference switch (aurora DEFAULT_FATAL) and nonfinite parameters are
// rejected here, before any material texture allocation.
MkwFog snapshot_gx_fog(const aurora::gx::GXRegisterState&);
}
