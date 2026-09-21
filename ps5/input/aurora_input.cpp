// SPDX-License-Identifier: GPL-3.0-only
#include "aurora_input.h"
#include "input.hpp"
#include <SDL3/SDL.h>
#define SDL_MAIN_HANDLED
#include <SDL3/SDL_main.h>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <stdexcept>

// Weak: the input host tests link this file without the game's async log.
extern "C" void mkw_log(const char* format, ...) __attribute__((weak, format(printf, 1, 2)));

namespace mkw::input {
namespace {
constexpr SDL_InitFlags subsystems = SDL_INIT_HAPTIC | SDL_INIT_JOYSTICK | SDL_INIT_GAMEPAD;
AuroraInput* active = nullptr;
}
AuroraInput::~AuroraInput() { (void)close(); }
bool AuroraInput::initialize(bool backgroundEvents) {
    if (initialized_) return true;
    if (active && active != this) return SDL_SetError("Aurora input already has an owner");
    if (native_.initialized()) return SDL_SetError("Native pad handles still await close");
    SDL_SetMainReady();
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, backgroundEvents ? "1" : "0");
    if (!SDL_InitSubSystem(subsystems)) return false;
    nativeError_ = native_.initialize();
    if (nativeError_ < 0) {
        SDL_QuitSubSystem(subsystems);
        if (native_.initialized()) active = this; // Failed rollback still owns handles.
        return SDL_SetError("Native pad initialization failed: 0x%08x", unsigned(nativeError_));
    }
    active = this;
    initialized_ = true;
#if defined(MKW_PLATFORM_PS5) && !defined(MKW_PS5_RELEASE)
    // Diagnostic menu/race autopilot: only a file placed next to Config.toml
    // enables it, so a normal package never injects input. Player packages
    // (MKW_PS5_RELEASE) do not even look for the file.
    if (std::ifstream file{"/app0/UserData/autopilot.txt"}) {
        std::stringstream text; text << file.rdbuf();
        auto script = parse_autopilot_script(text.str());
        if (!script.error.empty()) {
            std::fprintf(stderr, "[mkw-autopilot] script rejected, pilot off: %s\n", script.error.c_str());
        } else {
            std::fprintf(stderr, "[mkw-autopilot] DIAGNOSTIC input script loaded: %zu presses\n", script.presses.size());
        }
        autopilot_.load(std::move(script));
    }
#endif
    return true;
}
bool AuroraInput::rumble(void* context, uint16_t low, uint16_t high) {
    auto& target = *static_cast<RumbleTarget*>(context);
    const auto& current = target.owner->native_.slots()[target.slot];
    // A user change may reuse a slot before SDL drains its removal events.
    // Never send an old virtual device's callback to the new user's handle.
    if (current.user != target.user || current.handle != target.handle ||
        !current.signedIn || (!current.sample.connected && (low || high)) ||
        (current.sample.intercepted && (low || high))) return false;
    return target.owner->native_.vibrate(target.slot, uint8_t(low >> 8), uint8_t(high >> 8)) >= 0;
}
void AuroraInput::device_event(AuroraEventType type, SDL_JoystickID id) {
    AuroraEvent event{}; event.type = type; event.controller = id; events_.push_back(event);
}
const AuroraEvent* AuroraInput::poll() {
    if (!initialized_) throw std::logic_error("Aurora input is not initialized");
    events_.clear();
    nativeError_ = native_.refresh();
    native_.read();
    for (size_t i = 0; i < pads_.size(); ++i) {
        const auto& slot = native_.slots()[i];
        auto& target = targets_[i];
        if (target.user != slot.user || target.handle != slot.handle) {
            if (!pads_[i].close()) throw std::runtime_error(SDL_GetError());
            target = {this, i, slot.user, slot.handle};
            pads_[i].set_rumble(rumble, &target);
        }
        PadSample sample = slot.sample;
        if (i == 0 && autopilot_.enabled()) {
            const uint64_t now = SDL_GetTicksNS() / 1000;
            const uint32_t held = autopilot_.apply(sample, now);
            if (held != autopilotHeld_) {
                const double seconds = double(SDL_GetTicksNS()) * 1e-9;
                // Off the input thread when the game links the async log (ps5/runtime/async_log.h).
                if (&mkw_log) mkw_log("[mkw-autopilot] t=%.2fs buttons=0x%x\n", seconds, unsigned(held));
                else std::fprintf(stderr, "[mkw-autopilot] t=%.2fs buttons=0x%x\n", seconds, unsigned(held));
                autopilotHeld_ = held;
            }
        }
        if (!pads_[i].update(sample, int(i))) throw std::runtime_error(SDL_GetError());
        if (slot.readResult < 0 && !nativeError_) nativeError_ = slot.readResult;
    }
    aurora::input::set_mouse_scroll(0, 0);
    SDL_Event sdl;
    while (SDL_PollEvent(&sdl)) {
        switch (sdl.type) {
        case SDL_EVENT_GAMEPAD_ADDED: {
            const auto id = aurora::input::add_controller(sdl.gdevice.which);
            if (id != SDL_JoystickID(-1)) device_event(AURORA_CONTROLLER_ADDED, id);
            break;
        }
        case SDL_EVENT_GAMEPAD_REMAPPED:
            if (aurora::input::refresh_controller(sdl.gdevice.which))
                device_event(AURORA_CONTROLLER_ADDED, sdl.gdevice.which);
            break;
        case SDL_EVENT_GAMEPAD_REMOVED:
            aurora::input::remove_controller(sdl.gdevice.which);
            device_event(AURORA_CONTROLLER_REMOVED, sdl.gdevice.which);
            break;
        case SDL_EVENT_MOUSE_WHEEL: aurora::input::set_mouse_scroll(sdl.wheel.x, sdl.wheel.y); break;
        case SDL_EVENT_QUIT: { AuroraEvent event{}; event.type = AURORA_EXIT; events_.push_back(event); break; }
        default: break;
        }
        AuroraEvent event{}; event.type = AURORA_SDL_EVENT; event.sdl = sdl; events_.push_back(event);
    }
    events_.push_back(AuroraEvent{.type = AURORA_NONE});
    return events_.data();
}
int AuroraInput::close() {
    int failure = 0;
    if (initialized_) {
        for (auto& pad : pads_) {
            const auto id = pad.id();
            // Stop motors while the native target is still valid. PAD holds a
            // separate SDL reference which must be removed before SDL shutdown.
            if (!pad.close()) failure = -1;
            if (id) aurora::input::remove_controller(id);
        }
        SDL_QuitSubSystem(subsystems);
        initialized_ = false;
        events_.clear();
    }
    nativeError_ = native_.close();
    if (nativeError_ >= 0 && active == this) active = nullptr;
    return nativeError_ < 0 ? nativeError_ : failure;
}
}
