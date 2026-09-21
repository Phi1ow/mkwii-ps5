// SPDX-License-Identifier: GPL-3.0-only
// PC micro-benchmark of GxGeometry::snapshot_segments, the merged-draw vertex
// snapshot measured at ~3 ms per loaded race frame on PS5 (~430 groups built
// from ~8 000 primitive commands). One group: 20 triangle-strip commands of 12
// vertices with a direct matrix index and indexed position, normal and UV, the
// indices walking a local window of large shared arrays as course models do.
// It also prints a digest of every produced snapshot (records, indices, array
// ranges) so a change can be checked for identical output.
#include "gx_geometry.h"
#include <chrono>
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace mkw::agc;
namespace gx = aurora::gx;

static std::span<const uint8_t> resolve(void*, GXAttr, const gx::AttrArray& array, uint32_t offset, uint32_t bytes) {
    return {static_cast<const uint8_t*>(array.data) + offset, bytes};
}

int main(int argc, char** argv) {
    const unsigned groups = argc > 1 ? unsigned(std::atoi(argv[1])) : 200000;
    gx::GXRegisterState state{};
    state.vtxDesc[GX_VA_PNMTXIDX] = GX_DIRECT;
    state.vtxDesc[GX_VA_POS] = GX_INDEX16;
    state.vtxDesc[GX_VA_NRM] = GX_INDEX16;
    state.vtxDesc[GX_VA_TEX0] = GX_INDEX16;
    state.vtxFmts[0].attrs[GX_VA_POS] = {GX_POS_XYZ, GX_S16, 6, 0};
    state.vtxFmts[0].attrs[GX_VA_NRM] = {GX_NRM_XYZ, GX_S8, 6, 0};
    state.vtxFmts[0].attrs[GX_VA_TEX0] = {GX_TEX_ST, GX_S16, 10, 0};
    constexpr uint32_t kVertices = 16384;
    std::vector<uint8_t> positions(kVertices * 6), normals(kVertices * 3), uvs(kVertices * 4);
    for (size_t i = 0; i < positions.size(); ++i) positions[i] = uint8_t(i * 7);
    for (size_t i = 0; i < normals.size(); ++i) normals[i] = uint8_t(i * 3);
    for (size_t i = 0; i < uvs.size(); ++i) uvs[i] = uint8_t(i * 5);
    state.arrays[GX_VA_POS] = {positions.data(), uint32_t(positions.size()), 6, false, {}};
    state.arrays[GX_VA_NRM] = {normals.data(), uint32_t(normals.size()), 3, false, {}};
    state.arrays[GX_VA_TEX0] = {uvs.data(), uint32_t(uvs.size()), 4, false, {}};
    const auto layout = GxVertexLayout::build(state, GX_VTXFMT0);

    constexpr unsigned kSegments = 20, kPerSegment = 12;
    std::vector<GxDrawSegment> segments(kSegments, GxDrawSegment{GX_TRIANGLESTRIP, kPerSegment});
    segments[3].primitive = GX_TRIANGLES;
    segments[7].primitive = GX_QUADS;
    std::vector<uint8_t> records;
    uint32_t window = 1200;
    for (unsigned s = 0; s < kSegments; ++s)
        for (unsigned v = 0; v < kPerSegment; ++v) {
            const uint16_t index = uint16_t(window + (v * 37 + s * 11) % 300);
            records.push_back(uint8_t(v % 10 * 3));
            for (int attr = 0; attr < 3; ++attr) { records.push_back(uint8_t(index >> 8)); records.push_back(uint8_t(index)); }
        }

    uint64_t digest = 1469598103934665603ull;
    const auto mix = [&](const void* data, size_t size) {
        const auto* bytes = static_cast<const uint8_t*>(data);
        for (size_t i = 0; i < size; ++i) { digest ^= bytes[i]; digest *= 1099511628211ull; }
    };
    {
        auto check = GxGeometry::snapshot_segments(layout, state, segments, records, resolve, nullptr);
        mix(check.records().data(), check.records().size());
        mix(check.indices().data(), check.indices().size() * sizeof(uint32_t));
        for (const auto& array : check.arrays()) { mix(&array.sourceOffset, 4); mix(array.bytes.data(), array.bytes.size()); }
        const uint32_t header[] = {check.vertex_count(), uint32_t(check.primitive()), check.current_position_matrix()};
        mix(header, sizeof header);
    }
    size_t produced = 0;
    const auto started = std::chrono::steady_clock::now();
    for (unsigned g = 0; g < groups; ++g) {
        auto packet = GxGeometry::snapshot_segments(layout, state, segments, records, resolve, nullptr);
        produced += packet.indices().size();
    }
    const double nanos = double(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - started).count());
    std::printf("snapshot_segments: %u groups of %u commands (%zu vertices): %.1f ns/group, %.2f ns/command; "
                "indices %zu; output digest %016" PRIx64 "\n",
                groups, kSegments, records.size() / layout.stride, nanos / groups, nanos / groups / kSegments,
                produced / groups, digest);
    return 0;
}
