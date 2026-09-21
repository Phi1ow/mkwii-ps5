// SPDX-License-Identifier: GPL-3.0-only
#include <aurora/aurora.h>
#include <stdexcept>

// The VideoOut backend occupies the application display at its configured
// output mode. It cannot create a desktop window or switch a monitor's mode.
// The upstream settings UI reads back the active mode after each request and
// restores the selection if the requested mode could not be applied.
extern "C" AuroraDisplayMode aurora_get_display_mode() {
    return AURORA_DISPLAY_MODE_BORDERLESS;
}
extern "C" void aurora_set_display_mode(AuroraDisplayMode mode) {
    switch (mode) {
    case AURORA_DISPLAY_MODE_WINDOWED:
    case AURORA_DISPLAY_MODE_BORDERLESS:
    case AURORA_DISPLAY_MODE_EXCLUSIVE: return;
    default: throw std::invalid_argument("Invalid Aurora display mode");
    }
}
