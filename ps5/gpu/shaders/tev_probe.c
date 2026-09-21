/* SPDX-License-Identifier: GPL-3.0-only
 * Native pixel diagnostic for the shared TEV arithmetic; not a GX renderer.
 * One byte-addressed read-only buffer at s[0:3], eight reserved user dwords,
 * primitive interpolation mask s12 and perspective barycentrics v0/v1.
 * Each of the 64x32 cells selects a distinct 32-byte input record.
 */
#include "../tev_integer.h"
typedef unsigned u4 __attribute__((ext_vector_type(4)));
typedef __fp16 h2 __attribute__((ext_vector_type(2)));
extern u4 load_case(u4, unsigned, unsigned, unsigned)
    __asm("llvm.amdgcn.raw.buffer.load.v4i32");
extern void export_pixel(unsigned, unsigned, h2, h2, _Bool, _Bool)
    __asm("llvm.amdgcn.exp.compr.v2f16");

void tev_probe(u4 data, u4 reserved0, u4 reserved1,
               unsigned mask, float i, float j) {
    float x = __builtin_amdgcn_interp_p1(i, 0, 0, mask);
    float y = __builtin_amdgcn_interp_p1(i, 1, 0, mask);
    x = __builtin_amdgcn_interp_p2(x, j, 0, 0, mask);
    y = __builtin_amdgcn_interp_p2(y, j, 1, 0, mask);
    unsigned col = (unsigned)(x * 64.0f), row = (unsigned)(y * 32.0f);
    col = col < 64 ? col : 63; row = row < 32 ? row : 31;
    unsigned offset = (row * 64 + col) * 32;
    u4 abcd = load_case(data, offset, 0, 0);
    u4 state = load_case(data, offset + 16, 0, 0);
    unsigned flags = state.z;
    int bias = (int)((flags >> 8) & 3) * 128 - 128;
    int value = mkw_tev_component((int)abcd.x, (int)abcd.y,
        (int)abcd.z, (int)abcd.w, state.x, state.y,
        flags & 15, bias, (flags >> 4) & 3, (flags >> 6) & 1);
    /* Preserve negative/unclamped results in two UNorm bytes, rather than
     * silently clipping them at the framebuffer. Blue identifies the cell.
     */
    unsigned encoded = (unsigned)(value + 1024);
    /* Explicit UNorm reciprocal avoids the general LLVM divide sequence.
     * Its division-mode requirements are not established for this container.
     * Error is far below half an output byte for the whole 0..255 domain.
     */
    const float unorm8 = 0x1.010102p-8f;
    float r = (float)(encoded & 255) * unorm8;
    float g = (float)(encoded >> 8) * unorm8;
    float b = (float)(state.w & 255) * unorm8;
    export_pixel(0, 15, __builtin_amdgcn_cvt_pkrtz(r, g),
        __builtin_amdgcn_cvt_pkrtz(b, 1.0f), 1, 1);
}
