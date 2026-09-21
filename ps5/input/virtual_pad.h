// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "native_pad.h"
#include <SDL3/SDL_gamepad.h>

namespace mkw::input {
// Owns one virtual SDL gamepad fed by native snapshots. The caller initializes
// SDL_INIT_GAMEPAD and processes its add/remove events in Aurora's input loop.
class VirtualPad {
public:
    using Rumble = bool (*)(void*, uint16_t, uint16_t);
    VirtualPad() = default;
    VirtualPad(const VirtualPad&) = delete;
    VirtualPad& operator=(const VirtualPad&) = delete;
    ~VirtualPad();
    void set_rumble(Rumble callback, void* context) noexcept;
    bool update(const PadSample& sample, int initialPlayer);
    bool close();
    SDL_JoystickID id() const noexcept { return id_; }
    SDL_Gamepad* gamepad() const noexcept { return gamepad_; }
private:
    static bool SDLCALL rumble(void*, Uint16, Uint16);
    SDL_JoystickID id_ = 0;
    SDL_Gamepad* gamepad_ = nullptr;
    Rumble rumble_ = nullptr;
    void* context_ = nullptr;
};
}
