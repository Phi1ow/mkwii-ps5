// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "gx_direct_material.h"
#include "gx_geometry.h"
namespace mkw::agc {
// Renderer callbacks backed by actual WiiCompiled Memory and owning HLE
// vectors. Calls require exclusion of concurrent guest/host allocation changes.
class GxMemorySources {
public:
    static std::span<const uint8_t> read(const void*,size_t);
    static std::span<const uint8_t> array(void*,GXAttr,const aurora::gx::AttrArray&,uint32_t offset,uint32_t bytes);
    static GxSourceBytes texture(void*,const GXTexObj_&,const GXTlutObj_*);
};
}
