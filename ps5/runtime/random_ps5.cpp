// SPDX-License-Identifier: GPL-3.0-only
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <stdexcept>
extern "C" int sceRandomGetRandomNumber(void*, size_t);
extern "C" int sceSysmoduleLoadModule(unsigned short);
namespace {
[[noreturn]] void random_error(const char* operation, int rc) {
    char message[128];
    std::snprintf(message, sizeof(message), "PS5 random %s failed: 0x%08x", operation, unsigned(rc));
    throw std::runtime_error(message);
}
void load_random_module() {
    // SharpProspero SysmoduleId.Random. Optional application modules must be
    // loaded before their imported functions are called. Retain for the process.
    static std::once_flag loaded;
    std::call_once(loaded, [] {
        const int rc = sceSysmoduleLoadModule(0x00ba);
        if (rc < 0) random_error("module load", rc);
    });
}
}

extern "C" void arc4random_buf(void* destination, size_t size) {
    if (!size) return;
    if (!destination || size > UINTPTR_MAX - reinterpret_cast<uintptr_t>(destination))
        throw std::invalid_argument("Invalid native random output range");
    auto* bytes = static_cast<unsigned char*>(destination);
    try {
    load_random_module();
    // SharpProspero Interop/Random/SceRandom.cs limits each request to 64 bytes.
    for (size_t offset = 0; offset < size;) {
        const size_t count = size - offset < 64 ? size - offset : 64;
        const int rc = sceRandomGetRandomNumber(bytes + offset, count);
        if (rc < 0) random_error("entropy request", rc);
        offset += count;
    }
    } catch (...) {
        // Crypto++ must never receive a partly initialized random block.
        // Its C++ GenerateBlock caller can propagate this explicit failure.
        std::memset(destination, 0, size);
        throw;
    }
}
