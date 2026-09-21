#include "memory.h"
#include "memory_access.h"
#include "ppc_runtime.h"
#include <atomic>
#include <cstdio>
#include <string>
#include <unordered_map>
#include "../tests/deferred_memory_cases.h"
#include "../tests/dcbz_cases.h"
extern "C" void mkw_diagnostic_log(const char*);
// Only the memory subsystem is under test. No game instructions or graphics
// are run. The unrelated module/GX boundaries are explicit test observers.
extern "C" void GX_HLE_FIFO_Write8(uint8_t) {}
extern "C" void GX_HLE_FIFO_Write16(uint16_t) {}
extern "C" void GX_HLE_FIFO_Write32(uint32_t) {}
extern "C" void GX_HLE_FIFO_WriteFloat(float) {}
namespace SystemBridge { void DumpCpuState(const CpuContext*) {} }
namespace RecompMod {
std::atomic<bool> g_executableWriteGuardEnabled{false};
std::atomic<uint8_t> g_executableWriteGuardPages[kExecutableWriteGuardPageCount]{};
std::atomic<uint8_t> g_executableWriteGuardCoarsePages[kExecutableWriteGuardCoarsePageCount]{};
std::atomic<uint8_t> g_executableWriteGuardMidPages[kExecutableWriteGuardMidPageCount]{};
uint32_t CurrentTranslatedExecutionAddress() noexcept { return 0; }
bool HandleExecutableWrite(uint32_t, size_t, uint64_t) { return false; }
}
struct Deferred { unsigned calls = 0; };
static bool Materialize(void* user) {
    ++static_cast<Deferred*>(user)->calls;
    auto* bytes = GuestFlat::HostPointer(0x80002000);
    bytes[0] = 0x12; bytes[1] = 0x34; bytes[2] = 0x56; bytes[3] = 0x78;
    return true;
}
extern "C" int mkw_engine_memory_test() {
    mkw_diagnostic_log("[mkw-runtime] begin actual WiiCompiled Memory with libc++; module/GX hooks are test observers\n");
    try {
        std::unordered_map<std::string, unsigned> values;
        values[std::string("MEM") + "1"] = 24;
        values[std::string("MEM") + "2"] = 128;
        if (values.at("MEM2") != 128 || values.size() != 2) return 1;
        mkw_diagnostic_log("[mkw-runtime] PASS strings, hash table and heap\n");
        auto config = Memory::Config::WiiDefaults();
        Memory::Init(config);
        mkw_diagnostic_log("[mkw-runtime] Memory::Init complete\n");
        for (unsigned i = 0; i < 4096; ++i) {
            MemoryInline::FlatWriteRam32(0x80010000 + i * 4, i ^ 0xabcdef01);
            if (MemoryInline::FlatRead32(0xc0010000 + i * 4) != (i ^ 0xabcdef01)) return 2;
        }
        MemoryInline::FlatWriteRam32(Memory::kMem2CachedEnd - 4, 0x12345678);
        if (MemoryInline::FlatRead32(Memory::kMem2UncachedEnd - 4) != 0x12345678) return 3;
        Deferred read;
        if (!Memory::RegisterDeferredRead(0x80002000, 4096, Materialize, &read)) return 4;
        if (MemoryInline::FlatRead32(0x80002000) != 0x12345678 || read.calls != 1) return 5;
        Memory::ClearDeferredReads();
        char deferredMessage[192];
        std::snprintf(deferredMessage, sizeof(deferredMessage),
            "[mkw-deferred-memory] PASS %u owned RAM checks: aliases, cancellation, retry, boundary reads and partial stores\n",
            test_deferred_memory_cases());
        mkw_diagnostic_log(deferredMessage);
        std::snprintf(deferredMessage,sizeof(deferredMessage),
            "[mkw-dcbz] PASS %u actual helper checks: MEM1/MEM2 aliases, exact line clear, neighbours and conversion failure\n",test_dcbz_cases());
        mkw_diagnostic_log(deferredMessage);
        Memory::Init(config);
        if (Memory::Read32(Memory::kMem2CachedEnd - 4) != 0) return 6;
        mkw_diagnostic_log("[mkw-runtime] PASS actual engine layout, big-endian reads/writes, aliases, deferred EFB read and reinit\n");
        return 0;
    } catch (const std::exception& error) {
        mkw_diagnostic_log("[mkw-runtime] exception: "); mkw_diagnostic_log(error.what());
        mkw_diagnostic_log("\n"); return 7;
    }
}
