// SPDX-License-Identifier: GPL-3.0-only
// Host checks for snapshot_gx_fog against the reference packing in
// WiiCompiled aurora-main shader_info.cpp::fog_uniform(). Expected values are
// recomputed here from the register semantics rather than copied from the
// implementation. Two fields intentionally differ from the reference and are
// asserted as such: rangeBase.y carries the render-target width (the PS5
// shader divides an exported pixel x by it) and zmap carries the PA viewport
// depth scale/offset, neither of which the WGSL path needs.
#include "gx_fog_state.h"
#include <cmath>
#include <cstdio>
#include <stdexcept>

using mkw::agc::snapshot_gx_fog;
static unsigned checks;
static void check(bool v, int line = __builtin_LINE()) {
    ++checks;
    if (!v) throw std::runtime_error("GX fog check failed at line " + std::to_string(line));
}
template <class F> static void rejects(F action) {
    bool rejected = false;
    try { action(); } catch (const std::invalid_argument&) { rejected = true; }
    check(rejected);
}
static bool near(float a, float b) { return std::fabs(a - b) <= 1e-5f * std::max(1.f, std::fabs(b)); }

// A fully populated state: fog on, range on, distinct values in every field.
static aurora::gx::GXRegisterState populated() {
    aurora::gx::GXRegisterState s{};
    s.fog.type = GX_FOG_PERSP_LIN;
    s.fog.aRaw = 1.5f;
    s.fog.c = -0.25f;
    s.fog.bMagnitude = 0x123456u;
    s.fog.bShift = 7u;
    s.fog.color = {0.25f, 0.5f, 1.0f, 0.125f};
    s.xfViewport[0] = -320.f;                       // half-width, sign ignored
    s.renderViewport = {64.f, 8.f, 1280.f, 720.f, 0.25f, 0.75f};
    s.fogRange[0] = (1u << 10) | 400u;              // range adjust enabled, center 400
    for (unsigned i = 1; i < 6; ++i)
        s.fogRange[i] = ((i * 64u) << 12) | (i * 32u);
    return s;
}

int main() { try {
    // Fog off returns a zeroed block and never inspects the viewport.
    {
        aurora::gx::GXRegisterState s{};
        s.renderViewport = {0.f, 0.f, -1.f, 0.f, 0.f, 0.f};   // invalid, must be ignored
        const auto f = snapshot_gx_fog(s);
        check(f.ctl[0] == 0 && f.ctl[1] == 0);
        for (unsigned i = 0; i < 4; ++i) check(f.color[i] == 0.f && f.abc[i] == 0.f && f.rangeBase[i] == 0.f);
        for (float k : f.rangeK) check(k == 0.f);
    }

    // Every id the reference switch accepts, and only those. The reference
    // fatals on 0 handled above, on 8, and on anything past 15.
    for (unsigned type = 1; type < 16; ++type) {
        auto s = populated();
        s.fog.type = GXFogType(type);
        if (type == 8) { rejects([&] { snapshot_gx_fog(s); }); continue; }
        const auto f = snapshot_gx_fog(s);
        check(f.ctl[0] == type);
    }
    for (unsigned type : {16u, 17u, 255u}) {
        auto s = populated();
        s.fog.type = GXFogType(type);
        rejects([&] { snapshot_gx_fog(s); });
    }

    // Colour reaches the shader in TEV byte space; A/B/C packing keeps the raw
    // register values so the shader reconstructs B = bMagnitude >> bShift.
    {
        const auto s = populated();
        const auto f = snapshot_gx_fog(s);
        check(near(f.color[0], 0.25f * 255.f) && near(f.color[1], 0.5f * 255.f));
        check(near(f.color[2], 255.f) && near(f.color[3], 0.125f * 255.f));
        check(f.abc[0] == 1.5f && f.abc[1] == float(0x123456u));
        check(f.abc[2] == -0.25f && f.abc[3] == 7.f);
        check(f.ctl[1] == 1u);
    }

    // Range curve: ten 12-bit entries in 1/64 units, high half first, with the
    // last entry repeated into slots 10 and 11.
    {
        const auto s = populated();
        const auto f = snapshot_gx_fog(s);
        unsigned k = 0;
        for (unsigned i = 1; i < 6; ++i) {
            check(near(f.rangeK[k++], float((i * 64u)) / 64.f));
            check(near(f.rangeK[k++], float((i * 32u)) / 64.f));
        }
        check(f.rangeK[10] == f.rangeK[9] && f.rangeK[11] == f.rangeK[9]);
    }

    // Range centre: ((center - 342) / (|xfViewport[0]| * 2)) * 2 - 1, and the
    // render viewport mapping the shader needs to turn NDC x into pixel x.
    {
        const auto s = populated();
        const auto f = snapshot_gx_fog(s);
        const float expected = (float(400 - 342) / 640.f) * 2.f - 1.f;
        check(near(f.rangeBase[0], expected));
        check(near(f.rangeBase[1], 1280.f));            // render width, not Aurora's 640
        check(near(f.rangeBase[2], 640.f) && near(f.rangeBase[3], 64.f + 640.f));
        check(near(f.zmap[0], 0.5f) && near(f.zmap[1], 0.25f));
    }

    // Range disabled: no centre shift, and the divisor stays neutral.
    {
        auto s = populated();
        s.fogRange[0] = 400u;                            // bit 10 clear
        const auto f = snapshot_gx_fog(s);
        check(f.rangeBase[0] == 0.f && f.rangeBase[1] == 1.f && f.ctl[1] == 0u);
        check(near(f.rangeBase[2], 640.f));              // pixel mapping still published
    }

    // A degenerate XF width must not divide by zero; the reference clamps to 1.
    {
        auto s = populated();
        s.xfViewport[0] = 0.f;
        const auto f = snapshot_gx_fog(s);
        check(near(f.rangeBase[0], float(400 - 342) * 2.f - 1.f));
    }

    // Parameters that cannot be packed are refused before any material work.
    for (float bad : {std::nanf(""), INFINITY, -INFINITY}) {
        auto s = populated(); s.fog.aRaw = bad; rejects([&] { snapshot_gx_fog(s); });
        s = populated(); s.fog.c = bad; rejects([&] { snapshot_gx_fog(s); });
        for (unsigned i = 0; i < 4; ++i) {
            s = populated();
            (i == 0 ? s.fog.color.x() : i == 1 ? s.fog.color.y() : i == 2 ? s.fog.color.z() : s.fog.color.w()) = bad;
            rejects([&] { snapshot_gx_fog(s); });
        }
    }
    { auto s = populated(); s.fog.bShift = 32u; rejects([&] { snapshot_gx_fog(s); }); }
    { auto s = populated(); s.fog.bMagnitude = 0x1000000u; rejects([&] { snapshot_gx_fog(s); }); }
    { auto s = populated(); s.fog.bShift = 31u; s.fog.bMagnitude = 0xffffffu;
      check(snapshot_gx_fog(s).abc[3] == 31.f); }

    // The viewport feeds the pixel mapping, so a viewport that cannot be mapped
    // is an error rather than a silently wrong fog factor.
    for (unsigned field = 0; field < 5; ++field) for (float bad : {std::nanf(""), INFINITY}) {
        auto s = populated();
        auto& v = s.renderViewport;
        switch (field) {
            case 0: v.width = bad; break; case 1: v.left = bad; break;
            case 2: v.znear = bad; break; case 3: v.zfar = bad; break;
            default: v.width = 0.f; break;
        }
        rejects([&] { snapshot_gx_fog(s); });
    }
    { auto s = populated(); s.renderViewport.width = -1.f; rejects([&] { snapshot_gx_fog(s); }); }

    std::printf("PASS GX fog: %u checks, reference packing, range curve, viewport mapping and rejection paths\n", checks);
    return 0;
} catch (const std::exception& e) { std::fprintf(stderr, "FAIL after %u: %s\n", checks, e.what()); return 1; } }
