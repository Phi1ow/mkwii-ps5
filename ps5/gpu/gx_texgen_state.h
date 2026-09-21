// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "gx/register_state.hpp"
#include "gx_texgen.h"
namespace mkw::agc {
// usedMask is the set of coordinates required by the material. Unused texgen
// configurations do not need to be valid or supported by this shader path.
MkwTexgenState snapshot_gx_texgen(const aurora::gx::GXRegisterState&,unsigned usedMask);
}
static_assert(sizeof(MkwTexgen)==32 && sizeof(MkwTexgenState)==MKW_GX_TEXGEN_BYTES);
