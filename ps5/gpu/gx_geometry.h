// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "gx/register_state.hpp"
#include <span>
#include <vector>

namespace mkw::agc {
constexpr unsigned GxGeometryAttributeCount=GX_VA_TEX7+1;
struct GxAttributeLayout {
    GXAttrType mode=GX_NONE;
    GXCompType type=GX_F32;
    uint32_t offset=0, componentBytes=0, components=0, indexCount=1;
    uint32_t arrayStride=0, fraction=0;
    bool arrayLittleEndian=false;
};
struct GxVertexLayout {
    std::array<GxAttributeLayout,GxGeometryAttributeCount> attributes{};
    uint32_t stride=0;
    static GxVertexLayout build(const aurora::gx::GXRegisterState&,GXVtxFmt);
};
struct GxArraySnapshot {
    uint32_t sourceOffset=0;
    std::vector<uint8_t> bytes;
};
// Called only for the referenced interval after checking it against array.size.
// The memory owner validates the actual host/guest mapping before returning a
// span. Snapshot construction never dereferences array.data on its own.
using GxArrayResolver=std::span<const uint8_t> (*)(void*,GXAttr,
    const aurora::gx::AttrArray&,uint32_t offset,uint32_t bytes);
// One primitive command of a merged draw: its type and vertex count, in stream order.
struct GxDrawSegment {GXPrimitive primitive;uint16_t count;};
class GxGeometry {
public:
    static GxGeometry snapshot(const aurora::gx::GXRegisterState&,GXPrimitive,GXVtxFmt,
        uint16_t vertexCount,std::span<const uint8_t> records,GxArrayResolver,void* context);
    // Same, with the layout the caller already built from this state and format.
    static GxGeometry snapshot(const GxVertexLayout&,const aurora::gx::GXRegisterState&,GXPrimitive,
        uint16_t vertexCount,std::span<const uint8_t> records,GxArrayResolver,void* context);
    // Consecutive triangle-class primitive commands drawn under one unchanged
    // state: records are their concatenated vertex records, indices are each
    // segment's triangles offset by the vertices before it. Lines and points
    // are rejected; the result reports GX_TRIANGLES.
    static GxGeometry snapshot_segments(const GxVertexLayout&,const aurora::gx::GXRegisterState&,
        std::span<const GxDrawSegment>,std::span<const uint8_t> records,GxArrayResolver,void* context);
    const GxVertexLayout& layout() const noexcept{return layout_;}
    const std::vector<uint8_t>& records() const noexcept{return records_;}
    const std::array<GxArraySnapshot,GxGeometryAttributeCount>& arrays() const noexcept{return arrays_;}
    const std::vector<uint32_t>& indices() const noexcept{return indices_;}
    // Leaves this geometry without indices; only for a snapshot about to be discarded.
    std::vector<uint32_t> take_indices() noexcept{return std::move(indices_);}
    GXPrimitive primitive() const noexcept{return primitive_;}
    uint32_t vertex_count() const noexcept{return count_;}
    uint32_t current_position_matrix() const noexcept{return currentPnMtx_;}
private:
    // Records, current matrix and referenced array ranges; indices left empty.
    static GxGeometry snapshot_records(const GxVertexLayout&,const aurora::gx::GXRegisterState&,
        uint32_t vertexCount,std::span<const uint8_t> records,GxArrayResolver,void* context);
    GxVertexLayout layout_;
    std::vector<uint8_t> records_;
    std::array<GxArraySnapshot,GxGeometryAttributeCount> arrays_;
    std::vector<uint32_t> indices_;
    GXPrimitive primitive_=GX_TRIANGLES;
    uint32_t count_=0,currentPnMtx_=0;
};
// CPU reference/diagnostic reader for the raw GPU packet, derived from Aurora's
// attr_load/normal_group_load/fetch_* shader expressions. No transforms,
// lighting, texgen or CPU rasterization. Rendering keeps the original bytes.
struct GxDecodedVertex {
    std::array<float,3> position{0,0,0},normal{1,0,0},binormal{1,0,0},tangent{1,0,0};
    std::array<std::array<float,4>,2> colors{{{1,1,1,1},{1,1,1,1}}};
    std::array<std::array<float,2>,8> uv{};
    uint32_t positionMatrix=0;
    std::array<uint32_t,8> textureMatrices{~0u,~0u,~0u,~0u,~0u,~0u,~0u,~0u};
};
GxDecodedVertex decode_gx_vertex(const GxGeometry&,uint32_t vertex);
GXPrimitive gx_primitive_from_command(uint8_t command);
}
