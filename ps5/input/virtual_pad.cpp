// SPDX-License-Identifier: GPL-3.0-only
#include "virtual_pad.h"
#include <SDL3/SDL_stdinc.h>

namespace mkw::input {
namespace {
struct Button { SDL_GamepadButton sdl; uint32_t native; };
// SDL's virtual driver's auto-mapping packs only advertised buttons, in this
// enum order. PS/Create/mic are reserved by the platform and not advertised.
constexpr Button buttons[] = {
    {SDL_GAMEPAD_BUTTON_SOUTH, 0x4000}, {SDL_GAMEPAD_BUTTON_EAST, 0x2000},
    {SDL_GAMEPAD_BUTTON_WEST, 0x8000}, {SDL_GAMEPAD_BUTTON_NORTH, 0x1000},
    {SDL_GAMEPAD_BUTTON_START, 0x8},
    {SDL_GAMEPAD_BUTTON_LEFT_STICK, 0x2}, {SDL_GAMEPAD_BUTTON_RIGHT_STICK, 0x4},
    {SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, 0x400}, {SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, 0x800},
    {SDL_GAMEPAD_BUTTON_DPAD_UP, 0x10}, {SDL_GAMEPAD_BUTTON_DPAD_DOWN, 0x40},
    {SDL_GAMEPAD_BUTTON_DPAD_LEFT, 0x80}, {SDL_GAMEPAD_BUTTON_DPAD_RIGHT, 0x20},
    {SDL_GAMEPAD_BUTTON_TOUCHPAD, 0x100000},
};
}
VirtualPad::~VirtualPad() { (void)close(); }
void VirtualPad::set_rumble(Rumble callback, void* context) noexcept { rumble_ = callback; context_ = context; }
bool SDLCALL VirtualPad::rumble(void* context, Uint16 low, Uint16 high) {
    auto& pad = *static_cast<VirtualPad*>(context);
    return pad.rumble_ && pad.rumble_(pad.context_, low, high);
}
bool VirtualPad::update(const PadSample& sample, int initialPlayer) {
    if (!sample.connected) return close();
    if (!id_) {
        SDL_VirtualJoystickDesc desc{};
        SDL_INIT_INTERFACE(&desc);
        desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
        desc.naxes = 6;
        desc.nbuttons = sizeof(buttons) / sizeof(buttons[0]);
        desc.axis_mask = (1u << 6) - 1;
        for (const auto& button : buttons) desc.button_mask |= 1u << button.sdl;
        desc.name = "PS5 native controller";
        desc.userdata = this;
        if (rumble_) desc.Rumble = rumble;
        id_ = SDL_AttachVirtualJoystick(&desc);
        if (!id_) return false;
        gamepad_ = SDL_OpenGamepad(id_);
        if (!gamepad_) { SDL_DetachVirtualJoystick(id_); id_ = 0; return false; }
        if (!SDL_SetGamepadPlayerIndex(gamepad_, initialPlayer)) { close(); return false; }
    }
    auto* joystick = SDL_GetGamepadJoystick(gamepad_);
    for (unsigned i = 0; i < 6; ++i) {
        int value = sample.intercepted ? 0 : sample.axes[i];
        // SDL raw trigger axes span -32768..32767; SDL_GetGamepadAxis exposes
        // 0..32767. Feeding a neutral zero directly would become a half press.
        if (i >= 4) value = ((value < 0 ? 0 : value) * 65535 + 32766) / 32767 - 32768;
        if (!SDL_SetJoystickVirtualAxis(joystick, int(i), int16_t(value))) return false;
    }
    for (unsigned i = 0; i < sizeof(buttons) / sizeof(buttons[0]); ++i) {
        const bool held = !sample.intercepted && (sample.buttons & buttons[i].native);
        if (!SDL_SetJoystickVirtualButton(joystick, int(i), held)) return false;
    }
    // Flush the snapshot before Aurora PADRead queries gamepad axes/buttons.
    SDL_UpdateJoysticks();
    return true;
}
bool VirtualPad::close() {
    // Aurora can still hold another SDL reference. Detaching destroys the
    // driver's callback even then, so stop the native motors before detach.
    if (id_ && rumble_) (void)rumble_(context_, 0, 0);
    if (gamepad_) { SDL_CloseGamepad(gamepad_); gamepad_ = nullptr; }
    bool success = true;
    if (id_) { success = SDL_DetachVirtualJoystick(id_); id_ = 0; }
    return success;
}
}
