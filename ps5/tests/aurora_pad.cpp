// SPDX-License-Identifier: GPL-3.0-only
#include "virtual_pad.h"
#include "input.hpp"
#include <aurora/aurora.h>
#include <SDL3/SDL.h>
#define SDL_MAIN_HANDLED
#include <SDL3/SDL_main.h>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#ifdef MKW_AURORA_PAD_PROBE
extern "C" void mkw_diagnostic_log(const char*);
#endif
namespace aurora { AuroraConfig g_config{}; }
namespace {
int checks = 0;
void report(const char* text) {
#ifdef MKW_AURORA_PAD_PROBE
    mkw_diagnostic_log(text);
#else
    std::fputs(text, stdout);
#endif
}
void check(bool value) { ++checks; if (!value) {
    char text[384]; std::snprintf(text, sizeof(text), "[mkw-aurora-pad] FAIL check=%d error=%s\n", checks, SDL_GetError());
    report(text); throw std::runtime_error("Aurora PAD check");
} }
bool rumble(void*, uint16_t, uint16_t) { return true; }
void run(const char* directory) {
    namespace fs = std::filesystem;
    const fs::path root(directory), marker = root / "probe-owner.txt";
    constexpr const char* owner = "mkw-aurora-pad-native-v1";
    const bool reload = fs::exists(root);
    if (reload) {
        std::ifstream in(marker); std::string saved; std::getline(in, saved);
        check(in.good() && saved == owner); // Never modify an unrelated existing directory.
    } else {
        check(fs::create_directory(root));
    }
    aurora::g_config.userPath = directory;
    SDL_SetMainReady(); SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    SDL_SetHint(SDL_HINT_JOYSTICK_RAWINPUT, "0"); SDL_SetHint(SDL_HINT_JOYSTICK_WGI, "0"); SDL_SetHint(SDL_HINT_XINPUT_ENABLED, "0");
    aurora::input::initialize(); check(PADInit());
    mkw::input::VirtualPad pad;
    pad.set_rumble(rumble, nullptr);
    mkw::input::PadSample sample{}; sample.connected = true;
    check(pad.update(sample, 0)); check(aurora::input::add_controller(pad.id()) == pad.id());
    if (reload) {
        check(PADCount() == 1 && PADGetIndexForPort(2) >= 0 && PADGetIndexForPort(0) == -1);
        PADStatus restored[PAD_CHANMAX]{};
        sample.buttons = 0x4000; check(pad.update(sample, 0));
        check(PADRead(restored) == 0x20000000u && restored[2].err == PAD_ERR_NONE && restored[2].button == PAD_BUTTON_A);
        aurora::input::remove_controller(pad.id()); check(pad.close());
        aurora::input::shutdown(); SDL_Quit();
        check(fs::remove_all(root) == 3);
        report("[mkw-aurora-pad] PASS persisted port 2 restored after process restart, actual PADRead A button and own files cleaned\n");
        return;
    }
    check(PADCount() == 1 && PADGetIndexForPort(0) >= 0);
    PADStatus status[PAD_CHANMAX]{};
    auto read = [&] { check(pad.update(sample, 0)); return PADRead(status); };
    check(read() == 0x80000000u && status[0].err == PAD_ERR_NONE);
    for (unsigned i = 1; i < PAD_CHANMAX; ++i) check(status[i].err == PAD_ERR_NO_CONTROLLER);
    for (auto [native, expected] : std::array<std::pair<uint32_t, uint16_t>, 10>{{
        {0x4000, PAD_BUTTON_A}, {0x2000, PAD_BUTTON_B}, {0x8000, PAD_BUTTON_X}, {0x1000, PAD_BUTTON_Y},
        {0x8, PAD_BUTTON_START}, {0x800, PAD_TRIGGER_Z}, {0x10, PAD_BUTTON_UP}, {0x40, PAD_BUTTON_DOWN},
        {0x80, PAD_BUTTON_LEFT}, {0x20, PAD_BUTTON_RIGHT},
    }}) { sample.buttons = native; read(); check(status[0].button == expected); }
    sample.buttons = 0; sample.axes = {32767, -32768, -32768, 32767, 0, 0}; read();
    check(status[0].stickX == 127 && status[0].stickY == 127 && status[0].substickX == -127 && status[0].substickY == -127);
    sample.axes = {1000, -1000, 1000, -1000, 0, 0}; read();
    check(!status[0].stickX && !status[0].stickY && !status[0].substickX && !status[0].substickY);
    sample.axes[4] = sample.axes[5] = 32767; read();
    check((status[0].button & (PAD_TRIGGER_L | PAD_TRIGGER_R)) == (PAD_TRIGGER_L | PAD_TRIGGER_R));
    check(status[0].triggerLeft == 255 && status[0].triggerRight == 255);
    sample = {}; sample.connected = true; sample.buttons = 0x4000;
    PADBlockInput(true); read(); check(!status[0].button);
    PADBlockInput(false); read(); check(!status[0].button); // Suppress a held press on return from a menu.
    sample.buttons = 0; read(); sample.buttons = 0x4000; read(); check(status[0].button == PAD_BUTTON_A);
    PADSetButtonMapping(0, {SDL_GAMEPAD_BUTTON_EAST, PAD_BUTTON_A});
    sample.buttons = 0x2000; read(); check((status[0].button & PAD_BUTTON_A) != 0);
    PADRestoreDefaultMapping(0); read(); check(status[0].button == PAD_BUTTON_B);
    sample.intercepted = true; read(); check(!status[0].button && !status[0].triggerLeft);
    PADSetPortForIndex(PADGetIndexForPort(0), 2);
    check(PADGetIndexForPort(2) >= 0 && PADGetIndexForPort(0) == -1);
    const auto preferences = root / "controller_ports.dat";
    check(fs::is_regular_file(preferences) && fs::file_size(preferences) > 8);
    aurora::input::remove_controller(pad.id()); check(pad.close()); PADRead(status);
    check(PADCount() == 0 && status[0].err == PAD_ERR_NO_CONTROLLER);
    aurora::input::shutdown(); SDL_Quit();
    { std::ofstream out(marker); out << owner << '\n'; out.close(); check(!out.fail()); }
    char text[192]; std::snprintf(text, sizeof(text), "[mkw-aurora-pad] PASS %d checks through original WiiCompiled PAD; port 2 saved, restart required to validate reload\n", checks);
    report(text);
}
}
#ifdef MKW_AURORA_PAD_PROBE
extern "C" int mkw_test_aurora_pad() {
    checks = 0;
    report("[mkw-aurora-pad] begin original WiiCompiled PAD and preference persistence\n");
    try { run("/app0/mkw-pad-probe"); return 0; }
    catch (...) { SDL_Quit(); return 1; }
}
#else
int main(int argc, char** argv) {
    if (argc != 2) return 2;
    try { run(argv[1]); return 0; }
    catch (...) { SDL_Quit(); return 1; }
}
#endif
