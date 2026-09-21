/* SPDX-License-Identifier: GPL-3.0-only
 * Byte ABI for immutable raw GX geometry read by the native vertex shader.
 * All metadata is little-endian u32; vertex/array byte orders are preserved.
 */
#ifndef MKW_GX_VERTEX_FORMAT_H
#define MKW_GX_VERTEX_FORMAT_H
#define MKW_GX_VERTEX_MAGIC 0x31565847u
#define MKW_GX_VERTEX_HEADER_BYTES 32u
#define MKW_GX_VERTEX_ATTRIBUTE_BYTES 32u
#define MKW_GX_VERTEX_ATTRIBUTE_COUNT 21u
/* Header: magic, stride, recordsOffset, count; currentPnMtx, 0, 0, 0.
 * Attribute: flags, vertexOffset, arrayStride, arraySourceOffset;
 *            arrayDataOffset, arrayBytes, 0, 0.
 * Flags: mode[1:0], type[4:2], component count[8:5], index count[10:9],
 *        fraction[15:11], array little-endian[16].
 */
#endif
