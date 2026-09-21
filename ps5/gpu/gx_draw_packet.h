// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "gx_geometry_buffer.h"
#include "gx_transform_state.h"
#include "gx_lighting_state.h"
#include "gx_texgen_state.h"
#include "gx_direct_material.h"
#include "gx_blend_state.h"
#include "gx_viewport.h"
namespace mkw::agc {
// A complete owned snapshot for the current direct-TEV triangle shader ABI.
// Construct under renderer_gpu_mutex. No live GX state or guest pointers are
// retained. Materials keep textures and EFB copies alive across cache eviction.
struct GxDrawPacket {
    std::vector<uint8_t> geometry;
    std::vector<uint32_t> indices;
    GxTransformState transforms;
    MkwLighting lighting;
    MkwTexgenState texgen;
    GxDirectMaterial material;
    GxMappedViewport viewport;
    GxBlendState blend;
    uint32_t cullMode=0,depthControl=0;
    static GxDrawPacket build(const GxGeometry&,const aurora::gx::GXRegisterState&,
        GxTextureCache&,GxSourceResolver,void*,uint16_t anisotropy=1);
    // Same snapshot; takes ownership of the geometry's index list instead of copying it.
    static GxDrawPacket build(GxGeometry&&,const aurora::gx::GXRegisterState&,
        GxTextureCache&,GxSourceResolver,void*,uint16_t anisotropy=1);
};
// Uses WiiCompiled's reversed-Z comparison convention. The returned AGC
// depth test is enabled even for GX's ALWAYS path so depth writes still work.
uint32_t snapshot_gx_depth_control(const aurora::gx::GXRegisterState&);
}
