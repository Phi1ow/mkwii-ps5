// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "dolphin/gx/texture_object.hpp"
#include <array>

namespace mkw::agc {
// Aurora's sampler policy, encoded with SharpProspero's S# layout. The
// renderer supplies its configured maximum anisotropy (1,2,4,8,16).
// LOD bias belongs to the shader's sample-bias argument, as in Aurora;
// it is deliberately zero in this descriptor to avoid applying it twice.
std::array<uint32_t,4> gx_sampler(const GXTexObj_& texture, bool arbitraryMips,
                                uint16_t configuredAnisotropy);
}
