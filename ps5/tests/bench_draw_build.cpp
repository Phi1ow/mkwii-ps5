// SPDX-License-Identifier: GPL-3.0-only
// PC micro-benchmark of GxDrawPacket::build, the per-draw CPU cost measured on
// PS5. It times each step separately on a realistic draw: lit, fogged, two
// sampled textures, two texgens, three TEV stages, a 24-vertex strip with
// position/normal/UV. Only the physical-memory API is simulated. Absolute
// numbers are host-CPU figures; the step ratios are what guide PS5 work.
#include "gx_draw_packet.h"
#include "gx_fog_state.h"
#include "gx_tev_program.h"
#include "gx_geometry_buffer.h"
#include "hash.hpp"
#include "dolphin/gx/__gx.h"
#include <dolphin/gx/GXAurora.h>
#include <aurora/gfx.h>
#include <malloc.h>
#include <bit>
#include <chrono>
#include <cstdio>
#include <exception>
#include <map>
#include <stdexcept>
#include <vector>

static __GXData_struct shadow{};
__GXData_struct* __gx = &shadow;
struct Allocation { size_t bytes, alignment; void* address = nullptr; };
static std::map<int64_t, Allocation> physical;
static int64_t nextPhysical = 0x100000;
extern "C" uint64_t sceKernelGetDirectMemorySize() { return uint64_t{1} << 36; }
extern "C" int sceKernelAllocateDirectMemory(int64_t, int64_t, size_t bytes, size_t align, int, int64_t* out) {
    *out = nextPhysical; nextPhysical += int64_t(bytes); physical.emplace(*out, Allocation{bytes, align}); return 0;
}
extern "C" int sceKernelMapDirectMemory(void** out, size_t bytes, int, int, int64_t offset, size_t align) {
    *out = _aligned_malloc(bytes, align); physical.at(offset).address = *out; return *out ? 0 : -1;
}
extern "C" int sceKernelMunmap(void* address, size_t) {
    for (auto& [o, a] : physical) if (a.address == address) { _aligned_free(address); a.address = nullptr; return 0; }
    return -1;
}
extern "C" int sceKernelReleaseDirectMemory(int64_t offset, size_t) { physical.erase(offset); return 0; }
namespace aurora {
AuroraConfig g_config{};
std::chrono::nanoseconds wait_for_frame_worker_sealed() noexcept { return {}; }
}
namespace aurora::gx {
void set_logical_viewport(const gfx::Viewport&) noexcept { std::terminate(); }
void set_logical_scissor(const gfx::ClipRect&) noexcept { std::terminate(); }
void set_render_viewport(const gfx::Viewport&) noexcept { std::terminate(); }
void set_render_scissor(const gfx::ClipRect&) noexcept { std::terminate(); }
}
namespace aurora::gfx {
void push_debug_group(std::string) {}
void insert_debug_marker(std::string) {}
}
extern "C" void __GXSetDirtyState() {}
// The FIFO decoder links the draw receiver; this benchmark never decodes draws.
namespace aurora::gx::fifo {
bool handle_draw(u8, const u8*, u32&, u32, bool) { std::terminate(); }
}
extern "C" void aurora_pop_debug_group() {}

using namespace mkw::agc;
using Clock = std::chrono::steady_clock;
namespace gx = aurora::gx;

struct Sources { std::map<const void*, std::span<const uint8_t>> ranges; };
static GxSourceBytes resolve(void* ctx, const GXTexObj_& t, const GXTlutObj_*) {
    return {static_cast<Sources*>(ctx)->ranges.at(t.data), {}};
}
static std::vector<const void*> tracked;
static uint64_t generation(const void* data, size_t) {
    for (const void* p : tracked) if (p == data) return 1;
    return AURORA_GUEST_WRITE_UNTRACKED;
}

static void put_be(std::vector<uint8_t>& out, float f) {
    const uint32_t u = std::bit_cast<uint32_t>(f);
    for (int b = 3; b >= 0; --b) out.push_back(uint8_t(u >> (8 * b)));
}

int main(int argc, char** argv) { try {
    const unsigned iterations = argc > 1 ? unsigned(std::atoi(argv[1])) : 20000;
    aurora::g_config.logCallback = [](AuroraLogLevel, const char*, const char*, unsigned) {};

    gx::GXRegisterState s{};
    s.vtxDesc.fill(GX_NONE);
    s.vtxDesc[GX_VA_POS] = GX_DIRECT; s.vtxDesc[GX_VA_NRM] = GX_DIRECT; s.vtxDesc[GX_VA_TEX0] = GX_DIRECT;
    s.vtxFmts[0].attrs[GX_VA_POS] = {GX_POS_XYZ, GX_F32, 0};
    s.vtxFmts[0].attrs[GX_VA_NRM] = {GX_NRM_XYZ, GX_F32, 0};
    s.vtxFmts[0].attrs[GX_VA_TEX0] = {GX_TEX_ST, GX_F32, 0};
    for (auto& c : s.colorRegs) c = {0, 0, 0, 1};
    for (auto& c : s.kcolors) c = {1, 1, 1, 1};
    s.renderViewport = s.logicalViewport = {0, 0, 640, 480, 0, 1};
    s.renderScissor = s.logicalScissor = {0, 0, 640, 480};
    s.proj = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}};
    for (auto& m : s.pnMtx) { m.pos = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}}; m.nrm = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}; }
    s.cullMode = GX_CULL_BACK; s.depthCompare = true; s.depthFunc = GX_LEQUAL; s.depthUpdate = true;
    s.pixelFmt = GX_PF_RGB8_Z24; s.dstAlpha = UINT32_MAX; s.colorUpdate = s.alphaUpdate = true;
    // Two texgens from TEX0 through the identity matrices.
    s.numTexGens = 2;
    for (unsigned i = 0; i < 2; ++i) { s.tcgs[i].type = GX_TG_MTX2x4; s.tcgs[i].src = GX_TG_TEX0; s.tcgs[i].mtx = GX_IDENTITY; s.tcgs[i].postMtx = GX_PTIDENTITY; }
    // Three TEV stages: texture 0, modulated by texture 1, then a konst tint.
    s.numTevStages = 3;
    for (unsigned i = 0; i < 3; ++i) {
        auto& t = s.tevStages[i];
        t.texMapId = i < 2 ? GXTexMapID(i) : GX_TEXMAP_NULL;
        t.texCoordId = i < 2 ? GXTexCoordID(i) : GX_TEXCOORD_NULL;
        t.channelId = GX_COLOR0A0;
        t.colorPass = {GX_CC_ZERO, i ? GX_CC_CPREV : GX_CC_TEXC, i < 2 ? GX_CC_TEXC : GX_CC_KONST, GX_CC_ZERO};
        t.alphaPass = {GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, i ? GX_CA_APREV : GX_CA_TEXA};
    }
    // Lighting: channel 0 lit by two lights.
    s.numChans = 1;
    s.colorChannelConfig[0].lightingEnabled = true;
    s.colorChannelConfig[0].matSrc = GX_SRC_REG; s.colorChannelConfig[0].ambSrc = GX_SRC_REG;
    s.colorChannelConfig[0].diffFn = GX_DF_CLAMP; s.colorChannelConfig[0].attnFn = GX_AF_SPOT;
    s.colorChannelState[0].lightMask = 3;
    s.colorChannelState[0].matColor = {1, 1, 1, 1}; s.colorChannelState[0].ambColor = {0.2f, 0.2f, 0.2f, 1};
    for (unsigned i = 0; i < 2; ++i) {
        s.lights[i].dir = {0.3f, -1.0f, 0.2f, 0};
        s.lights[i].color = {1, 1, 1, 1};
        s.lights[i].cosAtt = {1, 0, 0, 0}; s.lights[i].distAtt = {1, 0, 0, 0};
    }
    // Fog on, as in a race.
    s.fog.type = GX_FOG_PERSP_LIN; s.fog.aRaw = 1.0f; s.fog.c = 0.25f; s.fog.bMagnitude = 0x8000; s.fog.bShift = 4;
    s.fog.color = {0.5f, 0.6f, 0.8f, 1};

    // Two textures typical of a kart/course material: 256x256 RGB565 and 128x128 RGBA8.
    Sources sources;
    std::vector<uint8_t> tex0(256 * 256 * 2, 0x5a), tex1(128 * 128 * 4, 0xa5);
    GXTexObj t0{}, t1{};
    GXInitTexObj(&t0, tex0.data(), 256, 256, GX_TF_RGB565, GX_REPEAT, GX_REPEAT, false);
    GXInitTexObj(&t1, tex1.data(), 128, 128, GX_TF_RGBA8, GX_CLAMP, GX_CLAMP, false);
    s.loadedTextures[0] = *reinterpret_cast<GXTexObj_*>(&t0);
    s.loadedTextures[1] = *reinterpret_cast<GXTexObj_*>(&t1);
    s.texCoordScales[0].scaleS = 255; s.texCoordScales[0].scaleT = 255;
    s.texCoordScales[1].scaleS = 127; s.texCoordScales[1].scaleT = 127;
    sources.ranges = {{tex0.data(), tex0}, {tex1.data(), tex1}};

    // 24-vertex triangle strip.
    std::vector<uint8_t> records;
    for (unsigned v = 0; v < 24; ++v) {
        put_be(records, float(v % 2)); put_be(records, float(v / 2) * 0.1f); put_be(records, -0.5f);
        put_be(records, 0); put_be(records, 0); put_be(records, 1);
        put_be(records, float(v % 2)); put_be(records, float(v / 2) / 12.0f);
    }
    const auto geometry = GxGeometry::snapshot(s, GX_TRIANGLESTRIP, GX_VTXFMT0, 24, records, nullptr, nullptr);

    GxTextureCache cache;
    // Warm-up: upload both textures once so the loop measures steady state.
    (void)GxDrawPacket::build(geometry, s, cache, resolve, &sources, 1);

    struct Step { const char* name; uint64_t nanos = 0; };
    const auto run = [&](const char* label, bool trackTextures) {
        tracked.clear();
        if (trackTextures) tracked = {tex0.data(), tex1.data()};
        aurora::g_guestWriteGenerationHook = &generation;
        (void)GxDrawPacket::build(geometry, s, cache, resolve, &sources, 1);  // prime the digest memo
        std::vector<Step> steps = {{"depth+viewport+blend"}, {"transforms"}, {"lighting"}, {"tev program"},
                                   {"texgen"}, {"fog"}, {"geometry serialize"}, {"indices copy"},
                                   {"material (textures)"}, {"full build"}, {"vertex snapshot"}};
        const auto time = [&](size_t index, auto&& body) {
            const auto t0 = Clock::now();
            body();
            steps[index].nanos += uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - t0).count());
        };
        for (unsigned i = 0; i < iterations; ++i) {
            time(0, [&] { (void)snapshot_gx_depth_control(s); (void)snapshot_gx_viewport({s.renderViewport, s.renderScissor}); (void)snapshot_gx_blend(s, 0); });
            time(1, [&] { (void)GxTransformState::snapshot(s); });
            time(2, [&] { (void)snapshot_gx_lighting(s); });
            time(3, [&] { (void)mkw::gpu::build_tev_program(s); });
            time(4, [&] { (void)snapshot_gx_texgen(s, mkw::gpu::tev_program_used_texcoords(s)); });
            time(5, [&] { (void)snapshot_gx_fog(s); });
            time(6, [&] { (void)serialize_gx_geometry(geometry); });
            time(7, [&] { std::vector<uint32_t> copy = geometry.indices(); (void)copy; });
            time(8, [&] { (void)GxDirectMaterial::build(s, cache, resolve, &sources, 1); });
            time(9, [&] { (void)GxDrawPacket::build(geometry, s, cache, resolve, &sources, 1); });
            time(10, [&] { (void)GxGeometry::snapshot(s, GX_TRIANGLESTRIP, GX_VTXFMT0, 24, records, nullptr, nullptr); });
        }
        std::printf("%s (%u iterations, µs per call)\n", label, iterations);
        for (const auto& step : steps)
            std::printf("  %-22s %8.2f\n", step.name, double(step.nanos) / iterations / 1000.0);
    };
    run("digest cache active (tracked guest RAM)", true);
    run("untracked sources (full rehash each draw)", false);
    for (auto& [o, a] : physical) (void)o, (void)a;
    return 0;
} catch (const std::exception& e) { std::fprintf(stderr, "FAIL %s\n", e.what()); return 1; } }
