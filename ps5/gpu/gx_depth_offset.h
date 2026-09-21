/* SPDX-License-Identifier: GPL-3.0-only
 * Tiled32_4 (SharpProspero tile mode 24, equation 27) byte offset for the
 * D32Float depth target. Shared by the host DepthLayout and the GX depth-copy
 * pixel shader so both address the exact same texels. x/y are pixel
 * coordinates inside the target; blocksPerRow = paddedWidth/128.
 */
#ifndef MKW_GX_DEPTH_OFFSET_H
#define MKW_GX_DEPTH_OFFSET_H
static inline unsigned mkw_depth_pixel_offset(unsigned x,unsigned y,unsigned blocksPerRow) {
    const unsigned block=(y>>7)*blocksPerRow+(x>>7);
    const unsigned inBlock=((y<<3)&0x8u)^((y<<4)&0x20u)^((y<<5)&0xf80u)^((y<<9)&0x1000u)^
        ((y<<8)&0x4000u)^((x<<2)&0x4u)^((x<<3)&0x10u)^((x<<4)&0x440u)^((x<<5)&0x300u)^
        ((x<<6)&0x800u)^((x<<9)&0xa000u);
    return (block<<16)+inBlock;
}
#endif
