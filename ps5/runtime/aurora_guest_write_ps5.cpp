// SPDX-License-Identifier: GPL-3.0-only
#include "hash.hpp"

// Preserve the hook registration from WiiCompiled's gfx/common.cpp without
// pulling in its WebGPU renderer. Registration retains upstream's callback
// lifetime contract; this adapter does not own or invoke the callbacks.
extern "C" void aurora_set_guest_write_hooks(AuroraGuestWriteGenerationCallback generation,
                                             AuroraGuestWriteNotifyCallback notify) {
    aurora::g_guestWriteGenerationHook = generation;
    aurora::g_guestWriteNotifyHook = notify;
}
