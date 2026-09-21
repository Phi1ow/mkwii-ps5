/* SPDX-License-Identifier: GPL-3.0-only
 * Integer TEV arithmetic translated from WiiCompiled Aurora's shader.cpp:
 * tev_regular_i32, tev_op, tev_color_op and tev_alpha_op.
 * Input selection, texture sampling, register writes and alpha test belong
 * to the renderer. This header implements one selected combiner component.
 * Valid ops: ADD=0, SUB=1, compares=8..15; scale=0..3; bias=-128/0/128.
 */
#ifndef MKW_TEV_INTEGER_H
#define MKW_TEV_INTEGER_H

/* Aurora quantizes AFTER floating-point overflow. A filtered value just below
 * 256 can therefore round to 256. Keep this core separate from the integer
 * convenience wrapper, which wraps integer inputs before calling it.
 */
static inline int mkw_tev_regular(int a, int b, int c, int d,
    unsigned op, int bias, unsigned scale) {
    int factor = scale == 1 ? 2 : scale == 2 ? 4 : 1;
    int interpolation = (a * 256 + (b - a) * (c + (c >> 7))) * factor;
    if (scale != 3) interpolation += op == 1 ? 127 : 128;
    int lerp = interpolation >> 8;
    int result = (d + bias) * factor + (op == 1 ? -lerp : lerp);
    return scale == 3 ? result >> 1 : result;
}

static inline int mkw_tev_component(int a, int b, int c, int d,
                                    unsigned color_a, unsigned color_b,
                                    unsigned op, int bias,
                                    unsigned scale, unsigned clamp) {
    a &= 255; b &= 255; c &= 255;
    int result;
    if (op < 2) {
        /* Multiplication avoids C's undefined left shift of negative d.
         * All intermediates fit i32 for the documented GX input domain.
         * The supported Clang CPU/GPU targets use arithmetic signed >>.
         */
        result = mkw_tev_regular(a, b, c, d, op, bias, scale);
    } else {
        unsigned lhs = (unsigned)a, rhs = (unsigned)b;
        /* Packed alpha compares also use the COLOR combiner A/B, as in
         * upstream tev_alpha_op. RGB8's alpha aliases compare scalar A/B.
         * Pack byte-space RGB as R | G<<8 | B<<16 before calling.
         */
        if (op < 14) {
            unsigned mask = op < 10 ? 255u : op < 12 ? 65535u : 16777215u;
            lhs = color_a & mask; rhs = color_b & mask;
        }
        unsigned pass = (op & 1) ? lhs == rhs : lhs > rhs;
        result = d + (pass ? c : 0);
    }
    int lo = clamp ? 0 : -1024, hi = clamp ? 255 : 1023;
    return result < lo ? lo : result > hi ? hi : result;
}

#endif
