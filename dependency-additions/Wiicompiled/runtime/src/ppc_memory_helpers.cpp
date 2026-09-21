#include "memory.h"
#include "recomp_mod_loader.h"
#include <cstdint>
#include <cstring>

// Separated from ppc_helpers.cpp so the memory helper is linked and exercised
// with the real Memory implementation independently of CPU/timebase services.
extern "C" int32_t memset_zero_32(int32_t address)
{
    // The translator aligns the effective dcbz address to a 32-byte line.
    const uint32_t addr = static_cast<uint32_t>(address);
    // Resolve before obtaining a raw pointer. In particular, zeroing a line
    // inside a pending EFB copy must preserve the copy's surrounding bytes.
    // This is outside the mapping fallback: conversion failure must propagate
    // once, not be hidden by a second attempt through eight scalar writes.
    MemoryInline::ResolveDeferredReads(addr, 32);
    if (RecompMod::ExecutableWriteGuardMayHit(addr, 32)) {
        for (uint32_t i = 0; i < 8; ++i) Memory::Write32(addr + i * 4, 0);
        return 0;
    }
    try {
        std::memset(Memory::GetPointer(addr, 32), 0, 32);
    } catch (const Memory::AccessViolation&) {
        // Keep the existing checked path for unmapped or MMIO destinations.
        for (uint32_t i = 0; i < 8; ++i) Memory::Write32(addr + i * 4, 0);
    }
    return 0;
}
