// SPDX-License-Identifier: GPL-3.0-only
#include "../input/native_pad.h"
#include <cstdio>
extern "C" void mkw_diagnostic_log(const char*);
extern "C" int sceKernelUsleep(unsigned);
extern "C" int scePadGetControllerInformation(int, void*);
extern "C" int scePadReadState(int, void*);
extern "C" int sceUserServiceGetForegroundUser(int*);
extern "C" int mkw_test_native_input() {
    using namespace mkw::input;
    NativePads pads;
    for (unsigned cycle = 0; cycle < 2; ++cycle) {
        int rc = pads.initialize();
        char message[256];
        if (rc < 0) {
            std::snprintf(message, sizeof(message), "[mkw-input] FAIL initialize 0x%08x\n", unsigned(rc));
            mkw_diagnostic_log(message); return 1;
        }
        int foreground = -1;
        const int foregroundResult = sceUserServiceGetForegroundUser(&foreground);
        std::snprintf(message, sizeof(message), "[mkw-input] foreground rc=0x%08x user=0x%08x\n", unsigned(foregroundResult), unsigned(foreground));
        mkw_diagnostic_log(message);
        for (const auto& slot : pads.slots()) {
            if (slot.handle < 0) continue;
            alignas(16) std::array<unsigned char, 1024> info{};
            const int infoResult = scePadGetControllerInformation(slot.handle, info.data());
            std::snprintf(message, sizeof(message), "[mkw-input] handle=%d user=0x%08x owned=%u infoRc=0x%08x connected=%u connectionType=%u connections=%u\n",
                slot.handle, unsigned(slot.user), unsigned(slot.owned), unsigned(infoResult), info[12], info[10], info[11]);
            mkw_diagnostic_log(message);
        }
        unsigned connected = 0, intercepted = 0, changed = 0, failures = 0, active = 0;
        std::array<uint64_t, 4> timestamps{};
        unsigned handles = 0;
        for (const auto& slot : pads.slots()) handles += slot.handle >= 0;
        for (unsigned n = 0; n < 120; ++n) {
            if (!(n % 60) && pads.refresh() < 0) ++failures;
            pads.read();
            for (size_t i = 0; i < pads.slots().size(); ++i) {
                const auto& slot = pads.slots()[i];
                if (slot.handle < 0) continue;
                const auto& sample = slot.sample;
                failures += slot.readResult < 0;
                connected += sample.connected;
                intercepted += sample.intercepted;
                changed += sample.timestampMicroseconds != timestamps[i];
                timestamps[i] = sample.timestampMicroseconds;
                active += sample.buttons != 0;
            }
            sceKernelUsleep(16667);
        }
        rc = pads.close();
        const bool passed = handles && connected && changed > 1 && !failures && rc >= 0;
        std::snprintf(message, sizeof(message), "[mkw-input] %s cycle=%u handles=%u connected=%u intercepted=%u timestampsChanged=%u buttonSamples=%u errors=%u close=0x%08x\n",
                      passed ? "PASS" : "FAIL", cycle, handles, connected, intercepted, changed, active, failures, unsigned(rc));
        mkw_diagnostic_log(message);
        if (!passed) return 1;
    }
    mkw_diagnostic_log("[mkw-input] PASS native UserService/Pad open, 240 polls, two close/reopen cycles; physical button mapping not yet validated\n");
    return 0;
}
