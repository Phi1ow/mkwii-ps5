// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "native_pad.h"
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace mkw::input {
// DIAGNOSTIC ONLY. Replays timed button presses into pad slot 0 so a
// performance run can walk the menus and drive a race with nobody holding the
// controller. It is inert unless a script file exists, and presses are ORed
// with the physical DualSense, which stays fully usable.
//
// Script lines: "<start seconds> <button>[+<button>...] [hold milliseconds]".
// "repeat <start> <end> <period ms> <button>[+...] [hold ms]" expands a
// periodic press. '#' starts a comment. Buttons use DualSense names: cross,
// circle, square, triangle, options, l1, r1, l3, r3, up, down, left, right.
struct AutopilotPress {
    uint64_t startMicros = 0, endMicros = 0;
    uint32_t buttons = 0;
    bool operator==(const AutopilotPress&) const = default;
};

struct AutopilotScript {
    std::vector<AutopilotPress> presses;
    std::string error;  // Empty when the whole script parsed.
};

AutopilotScript parse_autopilot_script(std::string_view text);
uint32_t autopilot_button(std::string_view name) noexcept;  // 0 when unknown

class PadAutopilot {
public:
    // Replaces any previous script. An invalid script leaves the pilot off.
    void load(AutopilotScript script);
    bool enabled() const noexcept { return !presses_.empty(); }
    // Buttons held at a time relative to the first apply() call.
    uint32_t buttons_at(uint64_t elapsedMicros) const noexcept;
    // Merges scripted presses into a sample. The first call starts the clock.
    // Returns the scripted buttons so the caller can log transitions.
    uint32_t apply(PadSample& sample, uint64_t nowMicros) noexcept;
private:
    std::vector<AutopilotPress> presses_;
    uint64_t origin_ = 0;
    size_t cursor_ = 0;  // First press that may still be active; time only moves forward.
    bool started_ = false;
};
}
