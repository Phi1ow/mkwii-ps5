// SPDX-License-Identifier: GPL-3.0-only
// Differential test of GxGeometry::snapshot_segments. Test-SnapshotEquivalence.ps1
// compiles it against the committed gx_geometry.cpp (reference) and the working
// tree (candidate). Each binary snapshots the same seeded random merged groups
// (1-40 commands of every triangle primitive and any vertex count, direct and
// indexed 8/16-bit attributes including NBT3 normals, random index windows)
// and prints a digest of every output: records, indices, array ranges and
// bytes, vertex count, primitive, current matrix, and exception messages.
#include "gx_geometry.h"
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <vector>

using namespace mkw::agc;
namespace gx = aurora::gx;

static uint64_t digest = 1469598103934665603ull;
static void mix(const void* data, size_t size) {
    const auto* bytes = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < size; ++i) { digest ^= bytes[i]; digest *= 1099511628211ull; }
}
static std::span<const uint8_t> resolve(void*, GXAttr, const gx::AttrArray& array, uint32_t offset, uint32_t bytes) {
    return {static_cast<const uint8_t*>(array.data) + offset, bytes};
}
struct Rng {
    uint64_t s;
    uint32_t next() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return uint32_t(s >> 11); }
    uint32_t below(uint32_t n) { return next() % n; }
};

int main(int argc, char** argv) {
    Rng r{argc > 1 ? std::strtoull(argv[1], nullptr, 0) * 0x9E3779B97F4A7C15ull + 1 : 1};
    const unsigned groups = argc > 2 ? unsigned(std::atoi(argv[2])) : 200000;
    std::vector<uint8_t> big(1 << 20);
    for (auto& b : big) b = uint8_t(r.next());
    static constexpr GXPrimitive kTriangles[] = {GX_QUADS, GX_TRIANGLES, GX_TRIANGLESTRIP, GX_TRIANGLEFAN};
    unsigned thrown = 0;
    for (unsigned g = 0; g < groups; ++g) {
        gx::GXRegisterState state{};
        state.currentPnMtx = r.below(10) * 3;
        if (r.below(2)) state.vtxDesc[GX_VA_PNMTXIDX] = GX_DIRECT;
        state.vtxDesc[GX_VA_POS] = GXAttrType(1 + r.below(3));
        state.vtxFmts[0].attrs[GX_VA_POS] = {GXCompCnt(r.below(2)), GXCompType(r.below(5)), uint8_t(r.below(8)), 0};
        if (r.below(2)) {
            state.vtxDesc[GX_VA_NRM] = GXAttrType(1 + r.below(3));
            state.vtxFmts[0].attrs[GX_VA_NRM] = {GXCompCnt(r.below(3)), GXCompType(r.below(5)), 6, 0};
        }
        if (r.below(2)) {
            state.vtxDesc[GX_VA_CLR0] = GXAttrType(1 + r.below(3));
            state.vtxFmts[0].attrs[GX_VA_CLR0] = {GX_CLR_RGBA, GXCompType(r.below(6)), 0, 0};
        }
        if (r.below(2)) {
            state.vtxDesc[GX_VA_TEX0] = GXAttrType(1 + r.below(3));
            state.vtxFmts[0].attrs[GX_VA_TEX0] = {GXCompCnt(r.below(2)), GXCompType(r.below(5)), uint8_t(r.below(12)), 0};
        }
        for (unsigned attr = GX_VA_POS; attr <= GX_VA_TEX7; ++attr) {
            const uint8_t stride = uint8_t(1 + r.below(12));
            state.arrays[attr] = {big.data() + r.below(4096), uint32_t(big.size() - 4096), stride, r.below(2) != 0, {}};
        }
        try {
            const auto layout = GxVertexLayout::build(state, GX_VTXFMT0);
            std::vector<GxDrawSegment> segments(1 + r.below(40));
            uint32_t vertices = 0;
            for (auto& segment : segments) {
                segment = {kTriangles[r.below(4)], uint16_t(r.below(20))};
                vertices += segment.count;
            }
            std::vector<uint8_t> records(size_t(vertices) * layout.stride);
            const uint32_t window = r.below(40000), span = 1 + r.below(r.below(2) ? 50 : 4000);
            for (uint32_t v = 0; v < vertices; ++v) {
                uint8_t* record = records.data() + size_t(v) * layout.stride;
                for (uint32_t i = 0; i < layout.stride; ++i) record[i] = uint8_t(r.next());
                for (unsigned attr = GX_VA_POS; attr <= GX_VA_TEX7; ++attr) {
                    const auto& d = layout.attributes[attr];
                    if (d.mode != GX_INDEX8 && d.mode != GX_INDEX16) continue;
                    for (unsigned k = 0; k < d.indexCount; ++k) {
                        const uint32_t index = d.mode == GX_INDEX8 ? r.below(256) : window + r.below(span);
                        if (d.mode == GX_INDEX16) {
                            record[d.offset + 2 * k] = uint8_t(index >> 8);
                            record[d.offset + 2 * k + 1] = uint8_t(index);
                        } else {
                            record[d.offset + k] = uint8_t(index);
                        }
                    }
                }
            }
            if (r.below(50) == 0 && !records.empty()) records.pop_back();  // size mismatch path
            auto out = GxGeometry::snapshot_segments(layout, state, segments, records, resolve, nullptr);
            mix(out.records().data(), out.records().size());
            mix(out.indices().data(), out.indices().size() * sizeof(uint32_t));
            for (const auto& array : out.arrays()) {
                mix(&array.sourceOffset, 4);
                const uint64_t n = array.bytes.size();
                mix(&n, 8);
                mix(array.bytes.data(), array.bytes.size());
            }
            const uint32_t header[] = {out.vertex_count(), uint32_t(out.primitive()), out.current_position_matrix()};
            mix(header, sizeof header);
        } catch (const std::exception& error) {
            ++thrown;
            mix(error.what(), std::strlen(error.what()));
        }
    }
    std::printf("groups %u exceptions %u digest %016" PRIx64 "\n", groups, thrown, digest);
    return 0;
}
