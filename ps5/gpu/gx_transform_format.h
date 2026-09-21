/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MKW_GX_TRANSFORM_FORMAT_H
#define MKW_GX_TRANSFORM_FORMAT_H
/* Rows of IEEE float32, matching Aurora matrix memory, not C++ pointers.
 * projection[4], pixelCenterCorrection.xy/renderViewport.wh, postex[20][3],
 * normal[10][3], postTexture[20][3]. All offsets are bytes. */
#define MKW_GX_TRANSFORM_PROJECTION 0u
#define MKW_GX_TRANSFORM_VIEWPORT 64u
#define MKW_GX_TRANSFORM_POSTEX 80u
#define MKW_GX_TRANSFORM_NORMAL 1040u
#define MKW_GX_TRANSFORM_POSTTEXTURE 1520u
#define MKW_GX_TRANSFORM_BYTES 2480u
#define MKW_GX_TRANSFORM_MATRIX_BYTES 48u
#endif
