// SPDX-License-Identifier: GPL-3.0-only
#include "../input/virtual_pad.h"
#include <SDL3/SDL.h>
#define SDL_MAIN_HANDLED
#include <SDL3/SDL_main.h>
#include <cstdio>
#include <stdexcept>
#include <array>
#include <unistd.h>
#include <cerrno>
#include <cstdlib>
#include <cstring>
extern "C" void mkw_diagnostic_log(const char*);
extern "C" int sceKernelUsleep(unsigned);
#ifdef MKW_AURORA_PAD_PROBE
extern "C" int mkw_test_aurora_pad();
#endif
namespace {
unsigned checks;
void check(bool condition, const char* operation) {
    ++checks;
    if (!condition) {
        const int posixError = errno;
        char text[384];
        std::snprintf(text, sizeof(text), "[mkw-sdl] FAIL check=%u operation=%s errno=%d error=%s\n", checks, operation, posixError, SDL_GetError());
        mkw_diagnostic_log(text);
        throw std::runtime_error(operation);
    }
}
std::array<Uint16, 2> motors{};
bool rumble(void*, Uint16 low, Uint16 high) { motors = {low, high}; return true; }
int SDLCALL worker(void*) { return 37; }
void cycle(unsigned index) {
    using namespace mkw::input;
    check(SDL_InitSubSystem(SDL_INIT_GAMEPAD | SDL_INIT_HAPTIC), "initialize gamepad and haptic subsystems");
    mkw_diagnostic_log("[mkw-sdl] initialized\n");
    auto* thread = SDL_CreateThread(worker, "mkw-sdl-test", nullptr);
    check(thread != nullptr, "create named thread");
    int result = 0;
    SDL_WaitThread(thread, &result);
    check(result == 37, "join named thread");
    mkw_diagnostic_log("[mkw-sdl] named thread joined\n");
    VirtualPad pad;
    pad.set_rumble(rumble, nullptr);
    PadSample sample{}; sample.connected = true;
    check(pad.update(sample, 0), "attach virtual pad");
    const auto first = pad.id();
    check(first && SDL_IsGamepad(first), "recognize gamepad");
    for (int raw = 0; raw < 256; ++raw) {
        sample.buttons = raw & 1 ? 0x4000 : 0;
        sample.axes = {int16_t(raw * 257 - 32768), int16_t(32767 - raw * 257), 0, 0,
            int16_t(raw * 32767 / 255), int16_t((255 - raw) * 32767 / 255)};
        check(pad.update(sample, 0), "submit snapshot");
        check(SDL_GetGamepadButton(pad.gamepad(), SDL_GAMEPAD_BUTTON_SOUTH) == bool(raw & 1), "cross to south");
        for (int axis = 0; axis < 6; ++axis)
            check(SDL_GetGamepadAxis(pad.gamepad(), SDL_GamepadAxis(axis)) == sample.axes[axis], "axis conversion");
    }
    sample.intercepted = true;
    check(pad.update(sample, 0), "intercept sample");
    check(!SDL_GetGamepadButton(pad.gamepad(), SDL_GAMEPAD_BUTTON_SOUTH), "intercept button neutral");
    for (int axis = 0; axis < 6; ++axis)
        check(SDL_GetGamepadAxis(pad.gamepad(), SDL_GamepadAxis(axis)) == 0, "intercept axis neutral");
    check(SDL_RumbleGamepad(pad.gamepad(), 1000, 2000, 1000), "virtual rumble");
    check(motors == std::array<Uint16, 2>{1000, 2000}, "rumble callback");
    auto* otherOwner = SDL_OpenGamepad(pad.id());
    check(otherOwner != nullptr, "second gamepad owner");
    check(pad.close(), "detach while another owner holds gamepad");
    check(motors == std::array<Uint16, 2>{}, "stop rumble on detach");
    check(!SDL_GamepadConnected(otherOwner), "second owner sees detach");
    SDL_CloseGamepad(otherOwner);
    sample = {}; sample.connected = true;
    check(pad.update(sample, 1) && pad.id() != first, "reconnect new instance");
    check(SDL_GetGamepadPlayerIndex(pad.gamepad()) == 1, "reconnect player");
    check(pad.close(), "close reconnected pad");
    SDL_Quit();
    char text[128];
    std::snprintf(text, sizeof(text), "[mkw-sdl] PASS cycle=%u cumulativeChecks=%u\n", index, checks);
    mkw_diagnostic_log(text);
}
}
extern "C" int mkw_test_sdl_input() {
    checks = 0;
    mkw_diagnostic_log("[mkw-sdl] begin native SDL synthetic input; no physical controller required\n");
    SDL_SetMainReady();
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
#ifdef MKW_INSPECT_SDL_IMPORTS
    char inspection[128];
    std::snprintf(inspection, sizeof(inspection), "[mkw-sdl] inspection pid=%d; hints initialized; waiting 90 seconds for read-only export inspection\n", getpid());
    mkw_diagnostic_log(inspection);
    for (unsigned second = 0; second < 90; ++second) sceKernelUsleep(1000000);
    mkw_diagnostic_log("[mkw-sdl] inspection complete; functional test skipped\n");
    return 0;
#endif
    try {
        char* current = SDL_GetCurrentDirectory();
        const bool unsupportedDirectory = !current && SDL_GetError()[0];
        SDL_free(current);
        check(unsupportedDirectory, "unsupported SDL current directory reports error");
        SDL_ClearError();
        check(SDL_strcmp(SDL_GetPlatform(), "PS5") == 0, "private platform name");
        for (unsigned index = 0; index < 2; ++index) cycle(index);
    } catch (...) { SDL_Quit(); return 1; }
    mkw_diagnostic_log("[mkw-sdl] PASS native SDL lifecycle, thread, buttons, axes, interception, rumble callback and reconnect; physical input not tested\n");
#ifdef MKW_AURORA_PAD_PROBE
    return mkw_test_aurora_pad();
#endif
    return 0;
}
