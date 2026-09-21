// SPDX-License-Identifier: GPL-3.0-only
#include "native_pad.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
using namespace mkw::input;
namespace {
int checks = 0;
void check(bool value) { ++checks; if (!value) { std::fprintf(stderr, "FAIL input check %d\n", checks); std::exit(1); } }
std::array<uint8_t, 1024> raw{};
std::array<int, 4> users{20, 10, -1, -1};
std::vector<int> opened, closed;
int listResult = 0, readResult = 0, closeResult = 0, initResult = 0;
int borrowed = -1, refused = -1, reads = 0;
std::array<int, 3> vibration{};
void buttons(uint32_t value) { std::memcpy(raw.data(), &value, 4); }
}
extern "C" int sceUserServiceInitialize(void*) { return -2; } // Already initialized by application.
extern "C" int sceUserServiceGetInitialUser(int* user) { *user = 10; return 0; }
extern "C" int sceUserServiceGetLoginUserIdList(int* out) { std::memcpy(out, users.data(), sizeof(users)); return listResult; }
extern "C" int scePadInit() { return initResult; }
extern "C" int scePadOpen(int user, int type, int index, void* param) {
    check(type == 0 && index == 0 && !param); opened.push_back(user);
    return user == refused ? -4 : user == borrowed ? int(0x80920004u) : user + 100;
}
extern "C" int scePadGetHandle(int user, int type, int index) { check(type == 0 && index == 0); return user + 100; }
extern "C" int scePadClose(int handle) { closed.push_back(handle); return closeResult; }
extern "C" int scePadReadState(int, void* out) { ++reads; std::memcpy(out, raw.data(), raw.size()); return readResult; }
extern "C" int scePadSetVibration(int handle, const void* params) {
    const auto* bytes = static_cast<const uint8_t*>(params); vibration = {handle, bytes[0], bytes[1]}; return 0;
}
int main() {
    for (size_t n = 0; n < 88; ++n) check(!decode_pad_sample(raw.data(), n).connected);
    check(!decode_pad_sample(nullptr, 1024).connected);
    raw[76] = 1; raw[4] = raw[5] = raw[6] = raw[7] = 128;
    uint64_t time = 0x123456789abcdef0ULL; std::memcpy(raw.data() + 80, &time, 8);
    auto decoded = decode_pad_sample(raw.data(), raw.size());
    check(decoded.connected && !decoded.intercepted && decoded.timestampMicroseconds == time);
    check(decoded.axes == std::array<int16_t, 6>{});
    int last = -32769;
    for (int n = 0; n < 256; ++n) {
        raw[4] = uint8_t(n); raw[8] = uint8_t(n); decoded = decode_pad_sample(raw.data(), 88);
        check(decoded.axes[0] > last); last = decoded.axes[0];
        check(decoded.axes[4] >= 0 && decoded.axes[4] <= 32767);
        if (!n) check(decoded.axes[0] == -32768 && decoded.axes[4] == 0);
        if (n == 128) check(decoded.axes[0] == 0);
        if (n == 255) check(decoded.axes[0] == 32767 && decoded.axes[4] == 32767);
    }
    buttons(0x0010ffff); decoded = decode_pad_sample(raw.data(), 88); check(decoded.buttons == 0x0010ffff);
    raw[76] = 0; decoded = decode_pad_sample(raw.data(), 88);
    check(!decoded.connected && !decoded.buttons && decoded.axes == std::array<int16_t, 6>{});
    raw[76] = 1; buttons(0x8010ffff); decoded = decode_pad_sample(raw.data(), 88);
    check(decoded.connected && decoded.intercepted && !decoded.buttons && decoded.axes == std::array<int16_t, 6>{});
    buttons(0x4000);
    NativePads pads;
    check(pads.refresh() < 0); check(pads.vibrate(0, 1, 2) < 0);
    initResult = -7; check(pads.initialize() == -7 && !pads.initialized()); initResult = 0;
    check(pads.initialize() == 0 && pads.initialized());
    check(opened == std::vector<int>({10, 20})); check(pads.slots()[0].user == 10 && pads.slots()[1].user == 20);
    check(pads.initialize() == 0 && opened.size() == 2);
    pads.read(); check(reads == 2 && pads.slots()[0].sample.buttons == 0x4000);
    readResult = -9; pads.read(); check(!pads.slots()[0].sample.connected && !pads.slots()[0].sample.buttons && pads.slots()[0].readResult == -9);
    readResult = 0; pads.read(); check(pads.slots()[0].sample.buttons == 0x4000);
    check(pads.vibrate(0, 17, 255) == 0 && vibration == std::array<int, 3>{110, 17, 255});
    check(pads.vibrate(4, 0, 0) < 0);
    listResult = -12; check(pads.refresh() == -12); int before = reads; pads.read();
    check(reads == before && !pads.slots()[0].sample.connected && pads.vibrate(0, 1, 1) < 0);
    listResult = 0; check(pads.refresh() == 0); pads.read(); check(pads.slots()[0].sample.connected);
    users = {20, 30, -1, -1}; borrowed = 30;
    check(pads.refresh() == 0); check(closed == std::vector<int>{110});
    check(pads.slots()[0].user == 30 && !pads.slots()[0].owned && pads.slots()[1].user == 20);
    users = {30, 40, -1, -1}; refused = 40; closeResult = -15;
    check(pads.refresh() == -15 && pads.slots()[1].handle == 120);
    pads.read(); check(!pads.slots()[1].sample.connected && !pads.slots()[1].signedIn);
    closeResult = 0; check(pads.refresh() == 0 && pads.slots()[1].handle < 0);
    refused = -1; check(pads.refresh() == 0 && pads.slots()[1].user == 40);
    before = int(closed.size()); check(pads.close() == 0 && !pads.initialized());
    check(int(closed.size()) == before + 1 && closed.back() == 140); // Borrowed handle never closed.
    check(pads.close() == 0 && int(closed.size()) == before + 1);
    check(pads.initialize() == 0); check(pads.close() == 0);
    listResult = -13; check(pads.initialize() == -13 && !pads.initialized());
    std::printf("PASS native input: %d checks (sample bounds, axis sweep, neutralization, stable slots, ownership, retry)\n", checks);
}
