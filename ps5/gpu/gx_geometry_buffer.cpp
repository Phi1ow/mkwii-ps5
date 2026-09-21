// SPDX-License-Identifier: GPL-3.0-only
#include "gx_geometry_buffer.h"
#include <cstring>
#include <limits>
#include <stdexcept>
namespace mkw::agc {
static_assert(GxGeometryAttributeCount==MKW_GX_VERTEX_ATTRIBUTE_COUNT);
static_assert(GX_VA_POS==9 && GX_VA_NRM==10 && GX_VA_CLR0==11 && GX_VA_TEX0==13);
static_assert(GX_DIRECT==1 && GX_INDEX8==2 && GX_INDEX16==3);
std::vector<uint8_t> serialize_gx_geometry(const GxGeometry& g) {
    // Size the buffer once: header, 4-byte aligned records and arrays, tail padding.
    size_t total=MKW_GX_VERTEX_HEADER_BYTES+MKW_GX_VERTEX_ATTRIBUTE_COUNT*MKW_GX_VERTEX_ATTRIBUTE_BYTES;
    total+=(g.records().size()+3)&~size_t(3);
    for(const auto& source:g.arrays())total+=(source.bytes.size()+3)&~size_t(3);
    std::vector<uint8_t> out;out.reserve(total+4);
    out.resize(MKW_GX_VERTEX_HEADER_BYTES+MKW_GX_VERTEX_ATTRIBUTE_COUNT*MKW_GX_VERTEX_ATTRIBUTE_BYTES,0);
    // Little-endian fields inside the header resized above (x86-64 host).
    auto write=[&](size_t at,uint32_t value){std::memcpy(out.data()+at,&value,4);};
    auto append=[&](std::span<const uint8_t> data){
        size_t at=out.size();
        if(data.size()>std::numeric_limits<uint32_t>::max()-at-7)throw std::length_error("GX GPU geometry buffer exceeds 32-bit byte offsets");
        out.resize((at+data.size()+3)&~size_t(3),0);if(!data.empty())std::memcpy(out.data()+at,data.data(),data.size());return uint32_t(at);
    };
    write(0,MKW_GX_VERTEX_MAGIC);write(4,g.layout().stride);write(12,g.vertex_count());write(16,g.current_position_matrix());
    write(8,append(g.records()));
    for(unsigned a=0;a<GxGeometryAttributeCount;++a) {
        const auto& d=g.layout().attributes[a];const auto& source=g.arrays()[a];size_t at=32+a*32;
        uint32_t flags=unsigned(d.mode)|(unsigned(d.type)<<2)|(d.components<<5)|(d.indexCount<<9)|(d.fraction<<11)|(unsigned(d.arrayLittleEndian)<<16);
        write(at,flags);write(at+4,d.offset);write(at+8,d.arrayStride);write(at+12,source.sourceOffset);
        if(!source.bytes.empty()){write(at+16,append(source.bytes));write(at+20,uint32_t(source.bytes.size()));}
    }
    // Aligned 32-bit loads may span the last element; explicit padding avoids
    // reading unowned bytes for an unaligned final u16/u24/u32.
    out.resize(out.size()+4,0);return out;
}
}
