// SPDX-License-Identifier: GPL-3.0-only
// Differential test of the guest memory access layer used by translated code.
//
// Test-MemoryAccessEquivalence.ps1 compiles this file twice with MKW_PLATFORM_PS5:
// once against a saved copy of the runtime headers/sources (reference) and
// once against the working tree (candidate), both with the real PS5 GuestFlat
// backend over a simulated kernel. Each binary runs the same seeded random
// program of guest accesses and prints a digest of everything observable:
// every loaded value (bit exact, including paired-single NaN/denormal lanes),
// every exception message, every materialization of a pending deferred read,
// every executable-write interception and gather-pipe write, the deferred-read
// registrations still pending, and the final bytes of MEM1, MEM2 and the
// locked cache. The two outputs must be identical.
//
// Addresses concentrate where the proofs differ: around pending deferred reads
// and their 1 MiB/4 KiB pages, page and mapping ends, executable-guarded pages,
// the MMIO/gather-pipe window and unmapped space, plus ordinary RAM.
#include "guest_flat_memory.h"
#include "guest_memory_ps5.h"
#include "memory.h"
#include "memory_access.h"
#include "ppc_runtime.h"
#include <cinttypes>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
uint64_t g_digest = 1469598103934665603ull;
uint64_t g_events = 0;
uint64_t g_rangesResolved = 0;  // informative, not part of the digest
bool g_verbose = false;
void mix(const void* data, size_t size) {
    const auto* bytes = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < size; ++i) { g_digest ^= bytes[i]; g_digest *= 1099511628211ull; }
}
void event(const char* format, ...) {
    char line[512];
    va_list args;
    va_start(args, format);
    const int n = std::vsnprintf(line, sizeof line, format, args);
    va_end(args);
    if (n > 0) mix(line, size_t(n) < sizeof line ? size_t(n) : sizeof line - 1);
    ++g_events;
    if (g_verbose) std::fputs(line, stdout);
}
struct Rng {
    uint64_t state;
    uint64_t next() { state ^= state << 13; state ^= state >> 7; state ^= state << 17; return state; }
    uint32_t below(uint32_t n) { return uint32_t(next() % n); }
};
[[noreturn]] void fail(const char* what) { std::fprintf(stderr, "FAIL %s\n", what); std::exit(1); }

// Simulated kernel for the PS5 GuestFlat backend (same ABI checks as
// guest_flat_ps5_test.cpp, without failure injection).
using Size = MkwPs5Memory::Size;
struct Allocation { long long physical; Size size; void* data; };
std::vector<Allocation>& allocations() { static std::vector<Allocation> a; return a; }
long long g_nextPhysical = 0;
bool g_reserved = false;
}  // namespace

extern "C" int sceKernelReserveVirtualRange(void** address, Size bytes, int flags, Size alignment) {
    if (alignment != 16384) fail("alignment");
    if (flags == 0x90) {
        if (g_reserved || bytes != MkwPs5Memory::GuestSpaceSize) fail("arena");
        g_reserved = true;
    }
    (void)address;
    return 0;
}
extern "C" Size sceKernelGetDirectMemorySize() { return 1024ull * 1024 * 1024; }
extern "C" int sceKernelAllocateDirectMemory(long long, long long, Size bytes, Size alignment, int type, long long* physical) {
    if (bytes % 16384 || alignment != 16384 || type != 11) fail("allocation ABI");
    void* memory = std::calloc(1, bytes);
    if (!memory) fail("host memory");
    *physical = g_nextPhysical;
    g_nextPhysical += static_cast<long long>(bytes);
    allocations().push_back({*physical, bytes, memory});
    return 0;
}
extern "C" int sceKernelMapDirectMemory(void** address, Size bytes, int prot, int flags, long long physical, Size) {
    if (prot != 3) fail("protection");
    for (auto& a : allocations()) {
        if (physical < a.physical || static_cast<Size>(physical - a.physical) + bytes > a.size) continue;
        if (!flags) *address = static_cast<unsigned char*>(a.data) + (physical - a.physical);
        return 0;
    }
    return -43;
}
extern "C" int sceKernelMunmap(void* address, Size bytes) {
    if (address == reinterpret_cast<void*>(MkwPs5Memory::GuestBase) && bytes == MkwPs5Memory::GuestSpaceSize) g_reserved = false;
    return 0;
}
extern "C" int sceKernelReleaseDirectMemory(long long physical, Size) {
    auto& a = allocations();
    for (size_t i = 0; i < a.size(); ++i) if (a[i].physical == physical) { std::free(a[i].data); a.erase(a.begin() + i); return 0; }
    return -44;
}

// Observers at the HLE/module boundaries: logged, identical in both builds.
extern "C" void GX_HLE_FIFO_Write8(uint8_t v) { event("fifo8 %02x\n", v); }
extern "C" void GX_HLE_FIFO_Write16(uint16_t v) { event("fifo16 %04x\n", v); }
extern "C" void GX_HLE_FIFO_Write32(uint32_t v) { event("fifo32 %08x\n", v); }
extern "C" void GX_HLE_FIFO_WriteFloat(float v) { uint32_t b; std::memcpy(&b, &v, 4); event("fifof %08x\n", b); }
namespace SystemBridge { void DumpCpuState(const CpuContext*) {} }
namespace RecompMod {
std::atomic<bool> g_executableWriteGuardEnabled{false};
std::atomic<uint8_t> g_executableWriteGuardPages[kExecutableWriteGuardPageCount]{};
std::atomic<uint8_t> g_executableWriteGuardCoarsePages[kExecutableWriteGuardCoarsePageCount]{};
std::atomic<uint8_t> g_executableWriteGuardMidPages[kExecutableWriteGuardMidPageCount]{};
uint32_t CurrentTranslatedExecutionAddress() noexcept { return 0; }
// Guarded pages behave like translated code: the store is intercepted.
bool HandleExecutableWrite(uint32_t address, size_t length, uint64_t value) {
    if (!g_executableWriteGuardEnabled.load()) return false;
    for (size_t i = 0; i < length; ++i) {
        if (g_executableWriteGuardPages[(address + i) >> 12].load()) {
            event("exec-write %08x %zu %016" PRIx64 "\n", address, length, value);
            return true;
        }
    }
    return false;
}
}  // namespace RecompMod

namespace {
struct Pending {
    uint32_t id, address;
    size_t length;
    uint64_t token;
};
std::vector<Pending> g_pending;
uint32_t g_nextId = 1;

bool materialize(void* user) {
    const auto id = uint32_t(uintptr_t(user));
    for (auto it = g_pending.begin(); it != g_pending.end(); ++it) {
        if (it->id != id) continue;
        event("materialize %u %08x %zu\n", id, it->address, it->length);
        auto* host = Memory::GetPointer(it->address, it->length);
        for (size_t i = 0; i < it->length; ++i) host[i] = uint8_t((id * 131u) ^ (i * 7u));
        g_pending.erase(it);
        return true;
    }
    fail("unknown deferred read");
}

struct Guarded { uint32_t start, pages; };
constexpr Guarded kCode[] = {{0x80004000u, 3}, {0x800fc000u, 2}, {0x90a00000u, 1}};

uint32_t pick_address(Rng& r, unsigned size) {
    switch (r.below(10)) {
    case 0: case 1: case 2: {  // around a pending deferred read
        if (g_pending.empty()) break;
        const auto& p = g_pending[r.below(uint32_t(g_pending.size()))];
        const uint32_t edge = r.below(2) ? p.address : p.address + uint32_t(p.length);
        return edge + r.below(33) - 16u;
    }
    case 3: {  // around a 1 MiB or 4 KiB page edge of RAM
        const uint32_t base = r.below(2) ? 0x80000000u : 0x90000000u;
        const uint32_t span = base == 0x80000000u ? 24u : 128u;
        const uint32_t page = (base + (r.below(span) << 20)) & ~(r.below(2) ? 0xfffffu : 0xfffu);
        return page + r.below(17) - 8u - size / 2;
    }
    case 4: {  // executable-guarded pages and their neighbours
        const auto& g = kCode[r.below(3)];
        return g.start - 16u + r.below(g.pages * 4096u + 32u);
    }
    case 5:  // mapping ends, locked cache, MMIO / gather pipe, unmapped
        switch (r.below(6)) {
        case 0: return 0x81800000u - 12u + r.below(16);  // MEM1 end (overlay starts here)
        case 1: return 0x98000000u - 12u + r.below(16);  // MEM2 end
        case 2: return 0xe0000000u + r.below(MemoryInline::kPageSize + 4096u);
        case 3: return 0xcc008000u + r.below(4);
        case 4: return 0xcc000000u + r.below(0x100);
        default: return 0x70000000u + r.below(0x1000);
        }
    default: break;
    }
    switch (r.below(4)) {  // ordinary RAM in every alias window
    case 0: return 0x80000000u + r.below(24u << 20);
    case 1: return 0x00000000u + r.below(24u << 20);
    case 2: return 0x90000000u + r.below(uint32_t(Memory::kMem2Size));
    default: return 0xd0000000u + r.below(uint32_t(Memory::kMem2Size));
    }
}

uint64_t random_bits(Rng& r) {
    static constexpr uint32_t kLanes[] = {0x00000000u, 0x80000000u, 0x00000001u, 0x007fffffu, 0x00800000u,
                                          0x7f800000u, 0xff800000u, 0x7f800001u, 0x7fc00000u, 0x3f800000u};
    const auto lane = [&]() { return r.below(3) ? uint32_t(r.next()) : kLanes[r.below(10)]; };
    return (uint64_t(lane()) << 32) | lane();
}

template <typename F>
void guarded(const char* op, uint32_t address, F&& body) {
    if (g_verbose) std::printf("? %s %08x\n", op, address), std::fflush(stdout);
    try {
        body();
    } catch (const std::exception& error) {
        event("%s %08x threw %s\n", op, address, error.what());
    }
}

void step(Rng& r) {
    const unsigned choice = r.below(100);
    if (choice < 4) {  // register a deferred RAM read like GXCopyTex does
        if (g_pending.size() >= 48) return;
        const uint32_t window = r.below(3) == 0 ? 0u : r.below(2) ? 0x80000000u : 0xc0000000u;
        const bool mem2 = r.below(2);
        const uint32_t length = r.below(4) ? 1u + r.below(0x2000) : 0x1000u * (1 + r.below(128));
        const uint32_t physical = mem2 ? 0x10000000u + r.below(uint32_t(Memory::kMem2Size) - length) : r.below((24u << 20) - length);
        const uint32_t id = g_nextId++;
        const uint32_t address = window | physical;
        const uint64_t token = Memory::RegisterDeferredRamRead(address, length, materialize,
            std::shared_ptr<void>(reinterpret_cast<void*>(uintptr_t(id)), [](void*) {}));
        event("register %u %08x %u -> %s\n", id, address, length, token ? "ok" : "refused");
        if (token) g_pending.push_back({id, physical | 0x80000000u, length, token});
        return;
    }
    if (choice < 7) {  // cancel one, as a replacing copy does
        if (g_pending.empty()) return;
        const size_t index = r.below(uint32_t(g_pending.size()));
        const Pending p = g_pending[index];
        const bool ok = Memory::CancelDeferredRead(p.token);
        event("cancel %u -> %d\n", p.id, int(ok));
        if (ok) g_pending.erase(g_pending.begin() + index);
        return;
    }
    const unsigned size = 1u << r.below(4);
    const uint32_t a = pick_address(r, size);
    switch (r.below(20)) {
    case 0: guarded("r8", a, [&] { event("r8 %08x %02x\n", a, MemoryInline::FlatRead8(a)); }); break;
    case 1: guarded("r16", a, [&] { event("r16 %08x %04x\n", a, MemoryInline::FlatRead16(a)); }); break;
    case 2: case 3: guarded("r32", a, [&] { event("r32 %08x %08x\n", a, MemoryInline::FlatRead32(a)); }); break;
    case 4: guarded("rf64", a, [&] { uint64_t b; const double d = MemoryInline::FlatReadFloat64(a); std::memcpy(&b, &d, 8); event("rf64 %08x %016" PRIx64 "\n", a, b); }); break;
    case 5: guarded("w8", a, [&] { const uint8_t v = uint8_t(r.next()); MemoryInline::FlatWrite8(a, v); event("w8 %08x %02x\n", a, v); }); break;
    case 6: guarded("w16", a, [&] { const uint16_t v = uint16_t(r.next()); MemoryInline::FlatWrite16(a, v); event("w16 %08x %04x\n", a, v); }); break;
    case 7: guarded("w32", a, [&] { const uint32_t v = uint32_t(r.next()); MemoryInline::FlatWrite32(a, v); event("w32 %08x %08x\n", a, v); }); break;
    case 8: guarded("wf32", a, [&] { double d; const uint64_t b = random_bits(r); std::memcpy(&d, &b, 8); MemoryInline::FlatWriteFloat32(a, d); event("wf32 %08x %016" PRIx64 "\n", a, b); }); break;
    case 9: guarded("r64", a, [&] { event("r64 %08x %016" PRIx64 "\n", a, Memory::Read64(a)); }); break;
    case 10: guarded("w64", a, [&] { const uint64_t v = r.next(); Memory::Write64(a, v); event("w64 %08x %016" PRIx64 "\n", a, v); }); break;
    case 11: case 12: case 13: case 14:  // paired-single float load (psq_l, GQR type 0)
        guarded("psql", a, [&] { PPC_FPR f; f.d = PpcLoadPairPsqFloatFastInline(a); event("psql %08x %016" PRIx64 "\n", a, f.raw); });
        break;
    case 15: case 16: case 17: case 18: {  // paired-single float store (psq_st, GQR type 0)
        PPC_FPR f; f.raw = random_bits(r);
        guarded("psqst", a, [&] { PpcStorePairPsqFloatFastInline(a, f.d); event("psqst %08x %016" PRIx64 "\n", a, f.raw); });
        break;
    }
    default: {  // translator-resolved range, then its scalar reads/writes
        const uint32_t length = 8u + r.below(64);
        const bool write = r.below(2);
        guarded("range", a, [&] {
            uint8_t* host = MemoryInline::ResolveRangeHost(a, 0, length, true, write);
            // Whether the range resolved to a host pointer is an internal choice
            // of path (the point of the optimization), not a guest effect: it is
            // counted apart and kept out of the digest.
            event("range %08x %u %d\n", a, length, int(write));
            g_rangesResolved += host != nullptr;
            const uint32_t offset = r.below(length - 7u) & ~3u;
            if (write) { const uint32_t v = uint32_t(r.next()); MemoryInline::WriteResolved32(host, offset, a + offset, v); event("rw32 %08x\n", v); }
            else event("rr32 %08x\n", MemoryInline::ReadResolved32(host, offset, a + offset));
        });
        break;
    }
    }
}
}  // namespace

int main(int argc, char** argv) {
    uint64_t seed = argc > 1 ? std::strtoull(argv[1], nullptr, 0) : 1;
    const uint64_t steps = argc > 2 ? std::strtoull(argv[2], nullptr, 0) : 2000000;
    g_verbose = argc > 3;
    const auto config = Memory::Config::WiiDefaults();
    std::vector<GuestFlat::RegionRequest> regions;
    for (const auto& region : config.regions) {
        using GuestFlat::Backing;
        Backing kind = Backing::Owned;
        if (region.baseAddress == Memory::kMem1PhysicalBase || region.baseAddress == Memory::kMem1CachedBase ||
            region.baseAddress == Memory::kMem1UncachedBase) kind = Backing::Mem1;
        if (region.baseAddress == Memory::kMem2PhysicalBase || region.baseAddress == Memory::kMem2CachedBase ||
            region.baseAddress == Memory::kMem2UncachedBase) kind = Backing::Mem2;
        regions.push_back({region.baseAddress, region.sizeBytes, kind});
    }
    GuestFlat::Initialize(regions);
    Memory::Init(config);
    Rng r{seed * 0x9E3779B97F4A7C15ull + 1};
    for (uint32_t i = 0; i < (24u << 20); ++i) GuestFlat::HostPointer(0x80000000u)[i] = uint8_t(r.next());
    for (uint32_t i = 0; i < uint32_t(Memory::kMem2Size); ++i) GuestFlat::HostPointer(0x90000000u)[i] = uint8_t(r.next());
    RecompMod::g_executableWriteGuardEnabled = true;
    for (const auto& g : kCode) {
        for (uint32_t p = 0; p < g.pages; ++p) {
            const uint32_t page = g.start + p * 4096u;
            RecompMod::g_executableWriteGuardCoarsePages[page >> 20] = 1;
            RecompMod::g_executableWriteGuardMidPages[page >> 16] = 1;
            RecompMod::g_executableWriteGuardPages[page >> 12] = 1;
        }
    }
    Memory::RefreshWritableFastPathsForExecutableRanges();
    for (uint64_t i = 0; i < steps; ++i) {
        step(r);
        if ((i + 1) % 500000 == 0) std::printf("step %" PRIu64 " digest %016" PRIx64 " events %" PRIu64 "\n", i + 1, g_digest, g_events);
    }
    for (const auto& p : g_pending) event("pending %u %08x %zu\n", p.id, p.address, p.length);
    mix(GuestFlat::HostPointer(0x80000000u), 24u << 20);
    mix(GuestFlat::HostPointer(0x90000000u), Memory::kMem2Size);
    mix(GuestFlat::HostPointer(0xe0000000u), MemoryInline::kPageSize + 4096u);
    std::printf("seed %" PRIu64 " steps %" PRIu64 " events %" PRIu64 " pending %zu final digest %016" PRIx64 "\n",
                seed, steps, g_events, g_pending.size(), g_digest);
    std::fprintf(stderr, "resolved ranges %" PRIu64 "\n", g_rangesResolved);
    // Skip static teardown: the simulated kernel and the GuestFlat singleton
    // have no defined destruction order (std::terminate at exit otherwise).
    std::fflush(stdout);
    std::_Exit(0);
}
