#pragma once
#include "register_state.hpp"

namespace aurora::gx::fifo {
// Shared WiiCompiled BP/CP/XF decoding. Drawing and renderer ownership are
// separate; these routines retain the original register calculations.
void handle_bp(u32 value, bool bigEndian);
void handle_cp(u8 address, u32 value, bool bigEndian);
void handle_xf(const u8* data, u32& position, u32 size, bool bigEndian);
bool copy_xf_data(u32 address, const u8* data, u32 count, bool bigEndian);
void reset_cp_register_cache();
} // namespace aurora::gx::fifo
