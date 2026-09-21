// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "gx_copy_texture_cache.h"
#include <vector>
namespace mkw::agc {
// Materialize a completed native-resolution copy into Wii's tiled, big-endian
// storage using WiiCompiled's encoder. Does not write guest RAM or register a
// deferred read. An enlarged primary target needs a completed nativeReadback
// target at its logical dimensions (linear GPU downscale for color copies).
// Both targets remain immutable and GPU-idle throughout this CPU transfer.
std::vector<uint8_t> encode_native_color_copy(const GxColorCopy&);
// GPU copies are submitted asynchronously; the renderer that owns the blitter
// installs the wait run before any CPU readback (nullptr uninstalls).
using GxCopyCompletionWait=void(*)(void*);
void set_gx_copy_completion_wait(GxCopyCompletionWait,void* context);
}
