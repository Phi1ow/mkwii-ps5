/* SPDX-License-Identifier: GPL-3.0-only
 * WiiCompiled GuestFlat implementation over the native PS5 memory backend.
 * Link instead of runtime/src/guest_flat_memory.cpp, with MKW_PLATFORM_PS5.
 * This translation unit needs the engine's C++ standard library and exceptions.
 */
#include "guest_flat_memory.h"
#include "guest_memory_ps5.h"
#include <cstdio>
#include <cstring>
#include <exception>
#include <memory>
#include <mutex>
#include <stdexcept>

static_assert(GuestFlat::RequiresCheckedAccess(), "PS5 requires checked Wii pages");
static_assert(GuestFlat::kFixedFlatGuestBase == MkwPs5Memory::GuestBase, "Guest base ABI");
namespace GuestFlat {
namespace {
using NativeBacking = MkwPs5Memory::Backing;
using NativeAlias = MkwPs5Memory::Alias;
constexpr uint64_t Granularity = 65536; // same region contract as WiiCompiled
struct Section {
    Backing kind = Backing::Owned;
    uint32_t ownedBase = 0;
    uint64_t bytes = 0;
    NativeBacking memory;
};
struct Mapping {
    NativeAlias alias;
    size_t section = 0;
    uint64_t offset = 0;
};
struct State {
    MkwPs5Memory::Arena arena;
    std::vector<RegionRequest> regions;
    std::unique_ptr<Section[]> sections;
    std::unique_ptr<Mapping[]> mappings;
    size_t sectionCount = 0;
    bool active = false;
    ~State() {
        // Failed initialization is transactional. Handles stay at stable addresses
        // until the last dependent alias has been released.
        bool failed = false;
        if (mappings) for (size_t i = regions.size(); i-- > 0;)
            if (MkwPs5Memory::UnmapAlias(mappings[i].alias)) failed = true;
        if (sections) for (size_t i = sectionCount; i-- > 0;)
            if (MkwPs5Memory::CloseBacking(sections[i].memory)) failed = true;
        if (MkwPs5Memory::CloseArena(arena)) failed = true;
        // Continuing after losing track of a live native mapping is unsafe.
        if (failed) std::terminate();
    }
};
std::unique_ptr<State>& Current() {
    static std::unique_ptr<State> state;
    return state;
}
std::mutex& Mutex() { static std::mutex mutex; return mutex; }
uint64_t Rounded(uint64_t bytes) { return (bytes + Granularity - 1) & ~(Granularity - 1); }
uint64_t Offset(const RegionRequest& region) {
    if (region.backing == Backing::Mem1) return region.base & 0x01ffffffu;
    if (region.backing == Backing::Mem2) return region.base & 0x0fffffffu;
    return 0;
}
void Check(int rc, const char* stage) {
    if (!rc) return;
    char message[160];
    std::snprintf(message, sizeof(message), "PS5 guest memory: %s failed (0x%08x)", stage, static_cast<unsigned>(rc));
    throw std::runtime_error(message);
}
bool Same(const std::vector<RegionRequest>& a, const std::vector<RegionRequest>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (a[i].base != b[i].base || a[i].size != b[i].size || a[i].backing != b[i].backing) return false;
    return true;
}
void Validate(const std::vector<RegionRequest>& regions) {
    for (size_t i = 0; i < regions.size(); ++i) {
        const auto& r = regions[i];
        if (!r.size) continue;
        if (r.backing != Backing::Owned && r.backing != Backing::Mem1 && r.backing != Backing::Mem2)
            throw std::runtime_error("PS5 guest memory: unknown backing kind");
        if (r.base % Granularity || r.size > kGuestSpaceSize - r.base)
            throw std::runtime_error("PS5 guest memory: unaligned or overflowing region");
        const uint64_t end = uint64_t(r.base) + Rounded(r.size);
        if (end > kGuestSpaceSize)
            throw std::runtime_error("PS5 guest memory: rounded region exceeds guest space");
        for (size_t j = 0; j < i; ++j) {
            const auto& other = regions[j];
            if (other.size && r.base < uint64_t(other.base) + Rounded(other.size) && other.base < end)
                throw std::runtime_error("PS5 guest memory: regions overlap after rounding");
        }
    }
}
}

bool IsActive() { return Current() && Current()->active; }
void Initialize(const std::vector<RegionRequest>& regions) {
    std::lock_guard<std::mutex> guard(Mutex());
    if (Current()) {
        if (!Same(Current()->regions, regions))
            throw std::runtime_error("PS5 guest memory: changing the layout requires restarting the process");
        for (size_t i = 0; i < Current()->sectionCount; ++i) {
            auto& memory = Current()->sections[i].memory;
            std::memset(memory.host, 0, memory.size);
        }
        return;
    }
    Validate(regions);
    auto candidate = std::make_unique<State>();
    candidate->regions = regions;
    candidate->sections = std::make_unique<Section[]>(regions.size());
    candidate->mappings = std::make_unique<Mapping[]>(regions.size());
    for (size_t i = 0; i < regions.size(); ++i) {
        const auto& region = regions[i];
        if (!region.size) continue;
        size_t index = 0;
        for (; index < candidate->sectionCount; ++index) {
            const auto& section = candidate->sections[index];
            if (section.kind == region.backing &&
                (region.backing != Backing::Owned || section.ownedBase == region.base)) break;
        }
        if (index == candidate->sectionCount) {
            auto& section = candidate->sections[candidate->sectionCount++];
            section.kind = region.backing; section.ownedBase = region.base;
        }
        auto& section = candidate->sections[index];
        auto& mapping = candidate->mappings[i];
        mapping.section = index; mapping.offset = Offset(region);
        uint64_t end = mapping.offset + Rounded(region.size);
        if (end > section.bytes) section.bytes = end;
    }
    Check(MkwPs5Memory::Reserve(candidate->arena), "reservation");
    for (size_t i = 0; i < candidate->sectionCount; ++i) {
        auto& section = candidate->sections[i];
        Check(MkwPs5Memory::OpenBacking(section.memory, Rounded(section.bytes)), "backing allocation");
        // Direct memory can have been used by an earlier allocation. The engine
        // expects pristine RAM even on its first initialization.
        std::memset(section.memory.host, 0, section.memory.size);
    }
    for (size_t i = 0; i < regions.size(); ++i) {
        const auto& region = regions[i];
        if (!region.size) continue;
        auto& mapping = candidate->mappings[i];
        auto& backing = candidate->sections[mapping.section].memory;
        Check(MkwPs5Memory::MapAlias(mapping.alias, candidate->arena, region.base,
            backing, mapping.offset, Rounded(region.size)), "guest alias");
    }
    // Unmapped and MMIO windows remain reserved and inaccessible. Checked
    // Memory::* paths implement EFB reads, executable guards and MMIO dispatch.
    candidate->active = true;
    Current() = std::move(candidate);
}
uint8_t* HostPointer(uint32_t address) {
    if (!IsActive()) return nullptr;
    const auto& state = *Current();
    for (size_t i = 0; i < state.regions.size(); ++i) {
        const auto& region = state.regions[i];
        if (address < region.base || uint64_t(address) - region.base >= region.size) continue;
        const auto& mapping = state.mappings[i];
        return static_cast<uint8_t*>(state.sections[mapping.section].memory.host) +
            mapping.offset + (uint64_t(address) - region.base);
    }
    return nullptr;
}
// These are deliberately inactive on hosts with pages larger than a Wii page,
// exactly as in the upstream RequiresCheckedAccess branch. Page policy lives
// in Memory's checked tables, not in coarser native mprotect ranges.
void ProtectDeferredRange(uint32_t, size_t) {}
void UnprotectDeferredRange(uint32_t, size_t) {}
void RegisterExecutableRange(uint32_t, uint32_t) {}
FaultCounters Counters() { return {}; }
bool HandleAccessViolation(void*, bool) noexcept { return false; }
void LogFaultSummary() noexcept {
    // There is no fault recovery on this backend; a zero fault count must not
    // be described as proof of complete MMIO emulation.
}
}
