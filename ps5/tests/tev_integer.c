/* SPDX-License-Identifier: GPL-3.0-only */
#include "../gpu/tev_integer.h"
__declspec(dllexport) int tev_host(int a, int b, int c, int d,
    unsigned ca, unsigned cb, unsigned op, int bias, unsigned scale, unsigned clamp) {
    return mkw_tev_component(a, b, c, d, ca, cb, op, bias, scale, clamp);
}
