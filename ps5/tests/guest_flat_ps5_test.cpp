#include "guest_flat_memory.h"
#include "guest_memory_ps5.h"
#include "memory.h"
#include "memory_access.h"
#include "ppc_runtime.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <vector>
#include "deferred_memory_cases.h"
#include "dcbz_cases.h"
#ifdef MKW_GPU_COPY_STORE_TEST
#include <malloc.h>
unsigned test_gx_copy_store();
#endif
#ifdef MKW_GX_MEMORY_SOURCES_TEST
unsigned test_gx_memory_sources();
#endif

// Kernel-call adapter for the actual native backend + actual GuestFlat adapter.
// Native alias coherence is tested on PS5 separately. Here we exercise engine
// region planning, host-pointer aliasing, zeroing and transactional rollback.
using Size = MkwPs5Memory::Size;
struct Allocation { long long physical; Size size; void* data; bool aligned=false; };
struct KernelState {
    std::vector<Allocation> allocations;
    int failAt = 0, calls = 0;
    bool reserved = false;
    long long next = 0;
    ~KernelState() { if (!allocations.empty() || reserved) std::abort(); }
};
static KernelState& Kernel() { static KernelState state; return state; }
static bool Fail() { auto& k = Kernel(); return ++k.calls == k.failAt; }
static void Require(bool condition, const char* what) {
    if (!condition) { std::fprintf(stderr, "FAIL %s\n", what); std::exit(1); }
}
// Observers at the unrelated HLE/module boundaries. Memory and its fast/slow
// tables below are the ported engine implementation, not test substitutes.
static unsigned fifoWrites, executableWrites;
static bool interceptExecutable;
extern "C" void GX_HLE_FIFO_Write8(uint8_t) { ++fifoWrites; }
extern "C" void GX_HLE_FIFO_Write16(uint16_t) { ++fifoWrites; }
extern "C" void GX_HLE_FIFO_Write32(uint32_t) { ++fifoWrites; }
extern "C" void GX_HLE_FIFO_WriteFloat(float) { ++fifoWrites; }
namespace SystemBridge { void DumpCpuState(const CpuContext*) {} }
namespace RecompMod {
std::atomic<bool> g_executableWriteGuardEnabled{false};
std::atomic<uint8_t> g_executableWriteGuardPages[kExecutableWriteGuardPageCount]{};
std::atomic<uint8_t> g_executableWriteGuardCoarsePages[kExecutableWriteGuardCoarsePageCount]{};
std::atomic<uint8_t> g_executableWriteGuardMidPages[kExecutableWriteGuardMidPageCount]{};
uint32_t CurrentTranslatedExecutionAddress() noexcept { return 0; }
bool HandleExecutableWrite(uint32_t address, size_t, uint64_t) {
    if (interceptExecutable && address == 0x80003000) { ++executableWrites; return true; }
    return false;
}
}
struct Deferred { uint32_t address; unsigned calls = 0; };
static bool Materialize(void* user) {
    auto& read = *static_cast<Deferred*>(user);
    ++read.calls;
    auto* bytes = GuestFlat::HostPointer(read.address);
    bytes[0] = 0x12; bytes[1] = 0x34; bytes[2] = 0x56; bytes[3] = 0x78;
    return true;
}
extern "C" int sceKernelReserveVirtualRange(void** address, Size bytes, int flags, Size alignment) {
    auto& k = Kernel();
    if (flags == 0x90) {
        Require(!k.reserved && bytes == MkwPs5Memory::GuestSpaceSize &&
            *address == reinterpret_cast<void*>(MkwPs5Memory::GuestBase), "exclusive arena");
        if (Fail()) return -40;
        k.reserved = true;
    } else Require(flags == 0x10 && k.reserved, "alias reservation replacement");
    Require(alignment == 16384, "native alignment");
    return 0;
}
extern "C" Size sceKernelGetDirectMemorySize() { return 1024ull * 1024 * 1024; }
extern "C" int sceKernelAllocateDirectMemory(long long, long long, Size bytes, Size alignment, int type, long long* physical) {
#ifdef MKW_GPU_COPY_STORE_TEST
    if(type==12){
        Require(alignment==2*1024*1024&&bytes%alignment==0,"GPU color allocation ABI");
        auto& k=Kernel();void* memory=_aligned_malloc(bytes,alignment);
        Require(memory!=nullptr,"aligned GPU storage");
        *physical=k.next;k.next+=static_cast<long long>(bytes);
        k.allocations.push_back({*physical,bytes,memory,true});return 0;
    }
#endif
    Require(bytes % 16384 == 0 && alignment == 16384 && type == 11, "allocation ABI");
    if (Fail()) return -41;
    auto& k = Kernel();
    void* memory = std::malloc(bytes);
    Require(memory != nullptr, "host memory");
    std::memset(memory, 0xa5, bytes);
    *physical = k.next; k.next += static_cast<long long>(bytes);
    k.allocations.push_back({*physical, bytes, memory});
    return 0;
}
extern "C" int sceKernelMapDirectMemory(void** address, Size bytes, int prot, int flags, long long physical, Size) {
    Require(prot == 3
#ifdef MKW_GPU_COPY_STORE_TEST
        || prot == 0x33
#endif
        , "CPU/GPU protection");
    if (Fail()) return -42;
    for (auto& a : Kernel().allocations) {
        if (physical < a.physical || static_cast<Size>(physical - a.physical) + bytes > a.size) continue;
        if (!flags) *address = static_cast<unsigned char*>(a.data) + (physical - a.physical);
        else Require(flags == 0x10 && Kernel().reserved, "alias inside reserved arena");
        return 0;
    }
    return -43;
}
extern "C" int sceKernelMunmap(void* address, Size bytes) {
    if (address == reinterpret_cast<void*>(MkwPs5Memory::GuestBase) && bytes == MkwPs5Memory::GuestSpaceSize)
        Kernel().reserved = false;
    return 0;
}
extern "C" int sceKernelReleaseDirectMemory(long long physical, Size) {
    auto& a = Kernel().allocations;
    for (size_t i = 0; i < a.size(); ++i) if (a[i].physical == physical) {
#ifdef MKW_GPU_COPY_STORE_TEST
        if(a[i].aligned){_aligned_free(a[i].data);a.erase(a.begin()+i);return 0;}
#endif
        std::free(a[i].data); a.erase(a.begin() + i); return 0;
    }
    return -44;
}
int main() {
    auto& kernel = Kernel(); // destroyed after GuestFlat's state
    using GuestFlat::Backing;
    const auto engineConfig = Memory::Config::WiiDefaults();
    std::vector<GuestFlat::RegionRequest> regions;
    for (const auto& region : engineConfig.regions) {
        Backing kind = Backing::Owned;
        if (region.baseAddress == Memory::kMem1PhysicalBase || region.baseAddress == Memory::kMem1CachedBase || region.baseAddress == Memory::kMem1UncachedBase) kind = Backing::Mem1;
        if (region.baseAddress == Memory::kMem2PhysicalBase || region.baseAddress == Memory::kMem2CachedBase || region.baseAddress == Memory::kMem2UncachedBase) kind = Backing::Mem2;
        regions.push_back({region.baseAddress, region.sizeBytes, kind});
    }
    // Every native allocation/mapping failure must undo all prior work and
    // leave a retry possible. 1 reserve + 4*(allocate+host-map) + 8 aliases.
    for (int step = 1; step <= 17; ++step) {
        kernel.calls = 0; kernel.failAt = step;
        bool threw = false;
        try { GuestFlat::Initialize(regions); } catch (const std::runtime_error&) { threw = true; }
        Require(threw && !GuestFlat::IsActive(), "failed init stays inactive");
        Require(kernel.allocations.empty() && !kernel.reserved, "failed init releases all native resources");
    }
    kernel.failAt = 0;
    for (const auto& invalid : std::vector<std::vector<GuestFlat::RegionRequest>>{
        {{0x80001000, 0x10000, Backing::Mem1}},
        {{0xffff0000, 0x10001, Backing::Owned}},
        {{0x80000000, 0x10001, Backing::Mem1}, {0x80010000, 0x10000, Backing::Mem1}}
    }) {
        bool threw = false;
        try { GuestFlat::Initialize(invalid); } catch (const std::runtime_error&) { threw = true; }
        Require(threw && kernel.allocations.empty() && !kernel.reserved, "invalid layout rejected before allocation");
    }
    GuestFlat::Initialize(regions);
    Require(GuestFlat::IsActive() && kernel.allocations.size() == 4, "engine layout has four backing stores");
    auto* mem1 = GuestFlat::HostPointer(0x80000000);
    auto* mem2 = GuestFlat::HostPointer(0x90000000);
    Require(mem1 == GuestFlat::HostPointer(0) && mem1 == GuestFlat::HostPointer(0xc0000000), "MEM1 aliases");
    Require(mem2 == GuestFlat::HostPointer(0x10000000) && mem2 == GuestFlat::HostPointer(0xd0000000), "MEM2 aliases");
    for (auto& a : kernel.allocations) {
        auto* data = static_cast<unsigned char*>(a.data);
        for (Size i = 0; i < a.size; ++i) Require(data[i] == 0, "fresh direct memory is cleared");
    }
    mem2[Memory::kMem2Size - 1] = 0x5a;
    Require(*GuestFlat::HostPointer(Memory::kMem2UncachedEnd - 1) == 0x5a, "NDEV upper MEM2 alias");
    Require(!GuestFlat::HostPointer(Memory::kMem2CachedEnd), "MEM2 end bounds");
    Require(!GuestFlat::HostPointer(0xcc000000) && !GuestFlat::HandleAccessViolation(reinterpret_cast<void*>(1), false), "MMIO and unrelated faults are not absorbed");
    Require(!GuestFlat::HostPointer(0xe0000000 + MemoryInline::kPageSize + 4096), "rounded tail is not guest-visible");
    auto changed = regions; changed[0].size -= 65536;
    bool threw = false;
    try { GuestFlat::Initialize(changed); } catch (const std::runtime_error&) { threw = true; }
    Require(threw && mem2[Memory::kMem2Size - 1] == 0x5a, "layout rejection preserves existing RAM");
    GuestFlat::Initialize(regions);
    Require(mem2 == GuestFlat::HostPointer(0x90000000) && mem2[Memory::kMem2Size - 1] == 0, "reinit clears RAM while preserving pointers");
    Memory::Init(engineConfig);
    MemoryInline::FlatWriteRam32(0x80000ffc, 0x87654321);
    Require(MemoryInline::FlatRead32(0xc0000ffc) == 0x87654321 && mem1[4092] == 0x87, "engine big-endian scalar and alias path");
    Deferred first{0x80001000}, second{0x80002000};
    Require(Memory::RegisterDeferredRead(first.address, 4096, Materialize, &first) != 0 &&
        Memory::RegisterDeferredRead(second.address, 4096, Materialize, &second) != 0, "register independent Wii pages");
    Require(MemoryInline::FlatRead32(first.address) == 0x12345678 && first.calls == 1 && second.calls == 0, "one EFB page materialized without its neighbour");
    Require(MemoryInline::FlatRead32(second.address) == 0x12345678 && second.calls == 1, "second EFB page materialized independently");
    std::printf("PASS %u owned deferred RAM checks: aliases, cancellation, retry, boundary reads and partial stores\n", test_deferred_memory_cases());
    std::printf("PASS %u actual dcbz helper checks: MEM1/MEM2 aliases, exact line clear, preserved neighbours and conversion failure\n",test_dcbz_cases());
#ifdef MKW_GX_MEMORY_SOURCES_TEST
    std::printf("PASS %u GX memory source checks: actual RAM aliases, deferred reads/retry, registered vector lifetimes, textures and palettes\n",test_gx_memory_sources());
#endif
#ifdef MKW_GPU_COPY_STORE_TEST
    std::printf("PASS %u GX copy-store checks: exact/overlapping replacements, aliases, eviction, bulk writes and retained owners\n",test_gx_copy_store());
    Require(kernel.allocations.size()==4,"copy-store tests released all simulated GPU allocations");
#endif
    interceptExecutable = true;
    RecompMod::g_executableWriteGuardEnabled = true;
    RecompMod::g_executableWriteGuardCoarsePages[0x80003000 >> 20] = 1;
    RecompMod::g_executableWriteGuardMidPages[0x80003000 >> 16] = 1;
    RecompMod::g_executableWriteGuardPages[0x80003000 >> 12] = 1;
    Memory::RefreshWritableFastPathsForExecutableRanges();
    MemoryInline::FlatWriteRam32(0x80003000, 0xabcdef01);
    Require(executableWrites == 1 && Memory::Read32(0x80003000) == 0, "generated RAM write retains executable interception");
    std::memset(Memory::GetPointer(0x80003000,32),0xaa,32);
    memset_zero_32(static_cast<int32_t>(0x80003000));
    Require(executableWrites==2&&Memory::Read32(0x80003000)==0xaaaaaaaa,
        "dcbz must not bypass the protected executable word");
    Require(Memory::Read32(0x80003004)==0&&Memory::Read32(0x8000301c)==0,
        "dcbz fallback sends every word through actual executable policy");
    MemoryInline::FlatWrite32(0xcc008000, 0x10203040);
    Require(fifoWrites == 1, "GPU FIFO routes to HLE");
    Memory::ClearDeferredReads();
    std::puts("PS5 GuestFlat: engine layout, NDEV MEM2, rollback, zeroing, aliases and bounds PASS");
    std::puts("Actual WiiCompiled Memory: endian access, separate EFB pages, executable guard and GX FIFO dispatch PASS");
}
