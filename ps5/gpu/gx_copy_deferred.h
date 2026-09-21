// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "gx_copy_texture_cache.h"
namespace mkw::agc {
// Validate immutable native-size storage without reading GPU pixels.
size_t native_color_copy_bytes(const GxColorCopy& copy);
// Register a completed immutable color copy with native-size readback storage.
// Memory owns the copy until materialization or CancelDeferredRead(token).
// Call with guest execution excluded; bulk HLE users of raw host pointers must
// ResolveDeferredReads(range) before reading/writing. Replacement/invalidation
// scheduling belongs to the GX copy backend, not this registration primitive.
uint64_t defer_native_color_copy(uint32_t guestAddress, ColorCopyHandle copy);
}
