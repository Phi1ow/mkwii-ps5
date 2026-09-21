// SPDX-License-Identifier: GPL-3.0-only
#include "native_pad.h"
#include <cstring>

namespace mkw::input {
PadSample decode_pad_sample(const void* data, size_t size) noexcept {
    PadSample result;
    if (!data || size < 88) return result;
    const auto* bytes = static_cast<const uint8_t*>(data);
    // PS5 and the host test target are little-endian. memcpy tolerates unaligned
    // input and avoids aliasing the opaque service sample as a C++ structure.
    uint32_t buttons;
    std::memcpy(&buttons, bytes, 4);
    result.connected = bytes[76] != 0;
    result.intercepted = (buttons & 0x80000000u) != 0;
    std::memcpy(&result.timestampMicroseconds, bytes + 80, 8);
    if (!result.connected || result.intercepted) return result;
    result.buttons = buttons;
    for (size_t i = 0; i < 4; ++i) {
        const int delta = int(bytes[4 + i]) - 128;
        // Both endpoints and neutral are exact. Aurora applies its dead zones;
        // applying one here as well would distort its configurable response.
        result.axes[i] = int16_t(delta < 0 ? delta * 256 : delta * 32767 / 127);
    }
    result.axes[4] = int16_t(int(bytes[8]) * 32767 / 255);
    result.axes[5] = int16_t(int(bytes[9]) * 32767 / 255);
    return result;
}
}
