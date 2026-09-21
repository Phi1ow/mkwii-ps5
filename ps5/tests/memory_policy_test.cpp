#include "memory_access.h"
#include "../runtime/guest_memory_ps5.h"
#include <cstdio>
#include <cstdlib>

namespace MemoryInline {
PageEntry g_pageTable[kPageCount]{};
uintptr_t g_fullPageBias[kPageCount]{};
uintptr_t g_fullReadablePageBias[kPageCount]{};
uintptr_t g_fullWritablePageBias[kPageCount]{};
const SparseWritablePageTable* g_sparseWritablePageTables[kPageCount]{};
uint8_t g_deferredReadCoveredPages[kPageCount]{};
}
namespace RecompMod {
std::atomic<uint8_t> g_executableWriteGuardPages[kExecutableWriteGuardPageCount]{};
}

static unsigned calls;
static uint32_t observedAddress;
static uint64_t observedValue;
static void observe(uint32_t address, uint64_t value) {
    ++calls; observedAddress = address; observedValue = value;
}
uint8_t MemoryInline::Read8Slow(uint32_t a) { observe(a, 8); return 0x81; }
uint16_t MemoryInline::Read16Slow(uint32_t a) { observe(a, 16); return 0x8123; }
uint32_t MemoryInline::Read32Slow(uint32_t a) { observe(a, 32); return 0x3f800000; }
uint64_t MemoryInline::Read64Slow(uint32_t a) { observe(a, 64); return 0x3ff0000000000000ull; }
void MemoryInline::Write8Slow(uint32_t a, uint8_t v) { observe(a, v); }
void MemoryInline::Write16Slow(uint32_t a, uint16_t v) { observe(a, v); }
void MemoryInline::Write32Slow(uint32_t a, uint32_t v) { observe(a, v); }
void MemoryInline::WriteFloat32Slow(uint32_t a, double v) { observe(a, static_cast<uint64_t>(v)); }
void MemoryInline::WriteFloat64Slow(uint32_t a, double v) { observe(a, static_cast<uint64_t>(v)); }
static void check(bool value) { if (!value) std::abort(); }
int main() {
    static_assert(GuestFlat::RequiresCheckedAccess());
    static_assert(GuestFlat::kFixedFlatGuestBase == MkwPs5Memory::GuestBase);
    // Adjacent Wii pages share a PS5 page; every access must still reach policy.
    // No flat mapping exists in this process, so bypassing policy would fault.
    for (uint32_t a : {0x80000ffcu, 0x80001000u, 0xcc008000u, 0xfffffffcu}) {
        unsigned before = calls;
        check(MemoryInline::FlatRead8(a) == 0x81);
        check(MemoryInline::FlatRead16(a) == 0x8123);
        check(MemoryInline::FlatRead32(a) == 0x3f800000);
        check(MemoryInline::FlatReadFloat32(a) == 1.0f);
        check(MemoryInline::FlatReadFloat64(a) == 1.0);
        MemoryInline::FlatWrite8(a, 0x81); check(observedValue == 0x81);
        MemoryInline::FlatWrite16(a, 0x8123); check(observedValue == 0x8123);
        MemoryInline::FlatWrite32(a, 0xabcdef01); check(observedValue == 0xabcdef01);
        MemoryInline::FlatWriteFloat32(a, 17.0); check(observedValue == 17);
        MemoryInline::FlatWriteFloat64(a, 29.0); check(observedValue == 29);
        check(calls == before + 10 && observedAddress == a);
        MemoryInline::FlatWriteRam8(a, 8);
        MemoryInline::FlatWriteRam16(a, 16);
        MemoryInline::FlatWriteRam32(a, 32);
        MemoryInline::FlatWriteRamFloat32(a, 17.0);
        MemoryInline::FlatWriteRamFloat64(a, 29.0);
        check(calls == before + 15 && observedAddress == a && observedValue == 29);
        check(MemoryInline::ResolveRangeHost(a, 0, 4, true, false) == nullptr);
        check(MemoryInline::ResolveRangeHost(a, 0, 4, false, true) == nullptr);
    }
    std::puts("PS5 memory policy: scalar and resolved accesses preserve checked dispatch");
}

