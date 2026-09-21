// SPDX-License-Identifier: GPL-3.0-only
#include <aurora/gfx.h>
#include <string>

// Match WiiCompiled gfx/common.cpp when AURORA_GFX_DEBUG_GROUPS is disabled.
// These are optional diagnostic labels, not guest GX commands or GPU fences.
#ifdef AURORA_GFX_DEBUG_GROUPS
#error PS5 GPU debug-group recording is not implemented
#endif
namespace aurora::gfx {
void push_debug_group(std::string) {}
void insert_debug_marker(std::string) {}
}
extern "C" void aurora_push_debug_group(const char*) {}
extern "C" void aurora_pop_debug_group() {}
