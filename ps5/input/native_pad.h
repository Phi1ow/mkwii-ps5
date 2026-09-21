// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace mkw::input {
// Layout/API provenance: SharpProspero Interop/Pad/Pad.cs and Input/GamePad.cs.
// Axes use SDL's signed 16-bit convention, ready for Aurora's existing mappings.
struct PadSample {
    uint32_t buttons = 0;
    std::array<int16_t, 6> axes{}; // LX, LY (down positive), RX, RY, L2, R2
    bool connected = false;
    bool intercepted = false;
    uint64_t timestampMicroseconds = 0;
};
PadSample decode_pad_sample(const void* data, size_t size) noexcept;

struct PadSlot {
    int user = -1;
    int handle = -1;
    bool owned = false;
    bool signedIn = false;
    int readResult = 0;
    PadSample sample{};
    bool vibrating = false; // Only vibration started through this NativePads owner.
};

// Single polling owner. Consumers copy snapshots instead of reading a device
// again for each button. Refresh keeps existing slots stable across user churn.
class NativePads {
public:
    NativePads() = default;
    NativePads(const NativePads&) = delete;
    NativePads& operator=(const NativePads&) = delete;
    ~NativePads();
    int initialize() noexcept;
    int refresh() noexcept;
    void read() noexcept;
    int close() noexcept;
    int vibrate(size_t slot, uint8_t low, uint8_t high) noexcept;
    const std::array<PadSlot, 4>& slots() const noexcept { return slots_; }
    bool initialized() const noexcept { return initialized_; }
private:
    std::array<PadSlot, 4> slots_{};
    bool initialized_ = false;
    int initialUser_ = -1;
};
}
