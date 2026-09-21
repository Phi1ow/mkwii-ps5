// SPDX-License-Identifier: GPL-3.0-only
#include "native_pad.h"
#include <algorithm>

extern "C" {
int sceUserServiceInitialize(void*);
int sceUserServiceGetInitialUser(int*);
int sceUserServiceGetLoginUserIdList(int*);
int scePadInit();
int scePadOpen(int, int, int, void*);
int scePadGetHandle(int, int, int);
int scePadClose(int);
int scePadReadState(int, void*);
int scePadSetVibration(int, const void*);
}

namespace mkw::input {
namespace {
constexpr int alreadyOpened = int(0x80920004u);
constexpr int notInitialized = int(0x80920005u);
constexpr int invalidArgument = int(0x80920001u);
int stop_slot(PadSlot& slot) noexcept {
    if (!slot.vibrating || slot.handle < 0) return 0;
    const std::array<uint8_t, 2> stopped{};
    const int rc = scePadSetVibration(slot.handle, stopped.data());
    if (rc >= 0) slot.vibrating = false;
    return rc;
}
int close_slot(PadSlot& slot) noexcept {
    slot.sample = {};
    slot.signedIn = false;
    const int stop = stop_slot(slot);
    if (stop < 0) return stop; // Retain even a borrowed handle until our motor stops.
    if (slot.owned && slot.handle >= 0) {
        const int rc = scePadClose(slot.handle);
        if (rc < 0) return rc; // Retain ownership so it cannot be silently reused.
    }
    slot = {};
    return 0;
}
}
NativePads::~NativePads() { (void)close(); }

int NativePads::initialize() noexcept {
    if (initialized_) return 0;
    // UserService can already belong to the application. Its query, rather
    // than an assumed 'already initialized' error code, establishes readiness.
    (void)sceUserServiceInitialize(nullptr);
    int rc = sceUserServiceGetInitialUser(&initialUser_);
    if (rc < 0) return rc;
    rc = scePadInit();
    if (rc < 0) return rc;
    initialized_ = true;
    rc = refresh();
    if (rc < 0) (void)close();
    return rc;
}

int NativePads::refresh() noexcept {
    if (!initialized_) return notInitialized;
    std::array<int, 4> users{-1, -1, -1, -1};
    const int rc = sceUserServiceGetLoginUserIdList(users.data());
    if (rc < 0) {
        // No authoritative user list: do not expose a stale held button.
        for (auto& slot : slots_) { (void)stop_slot(slot); slot.sample = {}; slot.signedIn = false; }
        return rc;
    }
    auto initial = std::find(users.begin(), users.end(), initialUser_);
    if (initialUser_ != -1 && initial != users.end()) std::iter_swap(users.begin(), initial);
    int failure = 0;
    for (auto& slot : slots_) {
        slot.signedIn = slot.handle >= 0 && std::find(users.begin(), users.end(), slot.user) != users.end();
        if (slot.handle < 0 || slot.signedIn) continue;
        const int result = close_slot(slot);
        if (result < 0 && !failure) failure = result;
    }
    for (int user : users) {
        if (user == -1) continue;
        if (std::any_of(slots_.begin(), slots_.end(), [=](const auto& s) { return s.user == user; })) continue;
        auto free = std::find_if(slots_.begin(), slots_.end(), [](const auto& s) { return s.handle < 0; });
        if (free == slots_.end()) break;
        int handle = scePadOpen(user, 0, 0, nullptr);
        const bool owned = handle != alreadyOpened;
        if (!owned) handle = scePadGetHandle(user, 0, 0);
        if (handle < 0) continue; // Signed-in users need not have a paired pad.
        *free = PadSlot{user, handle, owned, true, 0, {}};
    }
    return failure;
}

void NativePads::read() noexcept {
    for (auto& slot : slots_) {
        slot.sample = {};
        if (!initialized_ || slot.handle < 0 || !slot.signedIn) continue;
        alignas(16) std::array<uint8_t, 1024> bytes{};
        slot.readResult = scePadReadState(slot.handle, bytes.data());
        if (slot.readResult >= 0) slot.sample = decode_pad_sample(bytes.data(), bytes.size());
        if (!slot.sample.connected || slot.sample.intercepted) (void)stop_slot(slot);
    }
}

int NativePads::vibrate(size_t slot, uint8_t low, uint8_t high) noexcept {
    if (!initialized_) return notInitialized;
    if (slot >= slots_.size() || slots_[slot].handle < 0 || !slots_[slot].signedIn) return invalidArgument;
    const std::array<uint8_t, 2> params{low, high};
    const int rc = scePadSetVibration(slots_[slot].handle, params.data());
    if (rc >= 0) slots_[slot].vibrating = low || high;
    return rc;
}

int NativePads::close() noexcept {
    int failure = 0;
    for (auto& slot : slots_) {
        const int rc = close_slot(slot);
        if (rc < 0 && !failure) failure = rc;
    }
    if (!failure) initialized_ = false;
    // UserService is process-wide and may serve other subsystems; keep it alive.
    return failure;
}
}
