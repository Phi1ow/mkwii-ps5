// SPDX-License-Identifier: GPL-3.0-only
#include "virtual_pad.h"
#include <SDL3/SDL.h>
#define SDL_MAIN_HANDLED
#include <SDL3/SDL_main.h>
#include <cstdio>
#include <cstdlib>
#include <array>
using namespace mkw::input;
namespace {
int checks = 0;
void check(bool value) { ++checks; if (!value) { std::fprintf(stderr, "FAIL virtual pad check %d: %s\n", checks, SDL_GetError()); std::exit(1); } }
std::array<uint16_t, 2> motors{};
bool rumble(void* context, uint16_t low, uint16_t high) { check(context == &motors); motors = {low, high}; return true; }
}
int main() {
    SDL_SetMainReady();
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    SDL_SetHint(SDL_HINT_JOYSTICK_RAWINPUT, "0");
    SDL_SetHint(SDL_HINT_JOYSTICK_WGI, "0");
    SDL_SetHint(SDL_HINT_XINPUT_ENABLED, "0");
    check(SDL_InitSubSystem(SDL_INIT_GAMEPAD));
    {
        VirtualPad pad;
        pad.set_rumble(rumble, &motors);
        check(pad.update({}, 0) && !pad.id());
        PadSample sample{}; sample.connected = true;
        check(pad.update(sample, 2)); const auto first = pad.id();
        check(first && SDL_IsGamepad(first) && SDL_GetGamepadPlayerIndex(pad.gamepad()) == 2);
        for (int i = 0; i < SDL_GAMEPAD_AXIS_COUNT; ++i) check(SDL_GetGamepadAxis(pad.gamepad(), SDL_GamepadAxis(i)) == 0);
        check(!SDL_GamepadHasButton(pad.gamepad(), SDL_GAMEPAD_BUTTON_GUIDE));
        const std::array<std::pair<uint32_t, SDL_GamepadButton>, 14> pairs{{
            {0x4000, SDL_GAMEPAD_BUTTON_SOUTH}, {0x2000, SDL_GAMEPAD_BUTTON_EAST},
            {0x8000, SDL_GAMEPAD_BUTTON_WEST}, {0x1000, SDL_GAMEPAD_BUTTON_NORTH},
            {0x8, SDL_GAMEPAD_BUTTON_START}, {0x2, SDL_GAMEPAD_BUTTON_LEFT_STICK},
            {0x4, SDL_GAMEPAD_BUTTON_RIGHT_STICK}, {0x400, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER},
            {0x800, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER}, {0x10, SDL_GAMEPAD_BUTTON_DPAD_UP},
            {0x40, SDL_GAMEPAD_BUTTON_DPAD_DOWN}, {0x80, SDL_GAMEPAD_BUTTON_DPAD_LEFT},
            {0x20, SDL_GAMEPAD_BUTTON_DPAD_RIGHT}, {0x100000, SDL_GAMEPAD_BUTTON_TOUCHPAD},
        }};
        for (auto [native, expected] : pairs) {
            sample.buttons = native; check(pad.update(sample, 0));
            check(pad.id() == first && SDL_GetGamepadPlayerIndex(pad.gamepad()) == 2);
            for (int b = 0; b < SDL_GAMEPAD_BUTTON_COUNT; ++b)
                check(SDL_GetGamepadButton(pad.gamepad(), SDL_GamepadButton(b)) == (b == expected));
        }
        sample.buttons = 0;
        for (int i = 0; i < 6; ++i) {
            for (int value : {i < 4 ? -32768 : 0, 0, 12345, 32767}) {
                sample.axes[i] = int16_t(value); check(pad.update(sample, 0));
                check(SDL_GetGamepadAxis(pad.gamepad(), SDL_GamepadAxis(i)) == value);
            }
        }
        for (int raw = 0; raw < 256; ++raw) {
            sample.axes[4] = int16_t(raw * 32767 / 255);
            sample.axes[5] = int16_t((255 - raw) * 32767 / 255);
            check(pad.update(sample, 0));
            check(SDL_GetGamepadAxis(pad.gamepad(), SDL_GAMEPAD_AXIS_LEFT_TRIGGER) == sample.axes[4]);
            check(SDL_GetGamepadAxis(pad.gamepad(), SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) == sample.axes[5]);
        }
        sample.buttons = 0xffffffff; sample.intercepted = true; check(pad.update(sample, 0));
        for (int i = 0; i < 6; ++i) check(SDL_GetGamepadAxis(pad.gamepad(), SDL_GamepadAxis(i)) == 0);
        for (int b = 0; b < SDL_GAMEPAD_BUTTON_COUNT; ++b) check(!SDL_GetGamepadButton(pad.gamepad(), SDL_GamepadButton(b)));
        check(SDL_RumbleGamepad(pad.gamepad(), 1234, 5678, 1000)); check(motors == std::array<uint16_t, 2>{1234, 5678});
        check(SDL_SetGamepadMapping(first, "*,PS5 test mapping,a:b1,b:b0,leftx:a0,lefty:a1,rightx:a2,righty:a3,lefttrigger:a4,righttrigger:a5,"));
        sample = {}; sample.connected = true; sample.buttons = 0x4000; check(pad.update(sample, 0));
        check(SDL_GetGamepadButton(pad.gamepad(), SDL_GAMEPAD_BUTTON_EAST) && !SDL_GetGamepadButton(pad.gamepad(), SDL_GAMEPAD_BUTTON_SOUTH));
        check(pad.update({}, 0) && !pad.id()); check(motors == std::array<uint16_t, 2>{});
        check(!SDL_IsJoystickVirtual(first));
        check(pad.update(sample, 1)); check(pad.id() != first && SDL_GetGamepadPlayerIndex(pad.gamepad()) == 1);
        check(SDL_GetGamepadButton(pad.gamepad(), SDL_GAMEPAD_BUTTON_EAST)); // SDL retains the GUID mapping.
        auto* otherOwner = SDL_OpenGamepad(pad.id()); check(otherOwner == pad.gamepad());
        check(SDL_RumbleGamepad(otherOwner, 8000, 9000, 1000));
        check(motors == std::array<uint16_t, 2>{8000, 9000});
        check(pad.close()); check(motors == std::array<uint16_t, 2>{});
        check(!SDL_GamepadConnected(otherOwner)); SDL_CloseGamepad(otherOwner);
        check(pad.close());
    }
    SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
    std::printf("PASS virtual pad: %d checks through actual SDL (buttons, axes, remap, rumble, reconnect)\n", checks);
}
