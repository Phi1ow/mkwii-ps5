// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "virtual_pad.h"
#include "pad_autopilot.h"
#include <aurora/event.h>
#include <vector>

namespace mkw::input {
// One producer-thread owner, matching Aurora's SDL event/PAD ownership.
// The returned event array stays valid until the next poll or close.
class AuroraInput {
public:
    AuroraInput() = default;
    AuroraInput(const AuroraInput&) = delete;
    AuroraInput& operator=(const AuroraInput&) = delete;
    ~AuroraInput();
    bool initialize(bool backgroundEvents);
    const AuroraEvent* poll();
    int close();
    bool initialized() const noexcept { return initialized_; }
    int native_error() const noexcept { return nativeError_; }
    const NativePads& native() const noexcept { return native_; }
private:
    struct RumbleTarget { AuroraInput* owner = nullptr; size_t slot = 0; int user = -1; int handle = -1; };
    static bool rumble(void*, uint16_t, uint16_t);
    void device_event(AuroraEventType, SDL_JoystickID);
    NativePads native_;
    std::array<VirtualPad, 4> pads_;
    std::array<RumbleTarget, 4> targets_{};
    std::vector<AuroraEvent> events_;
    PadAutopilot autopilot_;       // Diagnostic; inert without a script file.
    uint32_t autopilotHeld_ = 0;   // Last scripted buttons, for transition logs.
    bool initialized_ = false;
    int nativeError_ = 0;
};
}
