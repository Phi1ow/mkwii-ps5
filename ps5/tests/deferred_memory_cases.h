// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "memory.h"
#include <array>
#include <cstring>
#include <memory>
#include <stdexcept>

// Shared PC/native cases; the real Memory implementation handles all accesses.
inline unsigned test_deferred_memory_cases() {
    unsigned checks = 0;
    auto check = [&](bool result) { ++checks; if (!result) throw std::runtime_error("Deferred memory regression"); };
    struct Copy {
        uint32_t address;
        unsigned calls = 0, failures = 0;
        uint8_t value = 0x5a;
        bool throwFailure = false, checkReentry = false;
        uint64_t token = 0;
    };
    const auto materialize = +[](void* context) -> bool {
        auto& c = *static_cast<Copy*>(context);
        ++c.calls;
        if (c.failures) {
            --c.failures;
            if (c.throwFailure) throw std::runtime_error("Injected conversion failure");
            return false;
        }
        if (c.checkReentry) {
            if (Memory::CancelDeferredRead(c.token)) throw std::logic_error("Cancelled running copy");
            bool rejected = false;
            try { Memory::ClearDeferredReads(); } catch (const std::logic_error&) { rejected = true; }
            if (!rejected) throw std::logic_error("Reset running copy");
            rejected = false;
            try { (void)Memory::Read8(c.address); } catch (const Memory::AccessViolation&) { rejected = true; }
            if (!rejected) throw std::logic_error("Recursive copy returned stale RAM");
        }
        std::memset(Memory::GetPointer(c.address, 16), c.value, 16);
        return true;
    };
    auto schedule = [&](uint32_t addr) {
        auto c = std::make_shared<Copy>(); c->address = addr;
        std::memset(Memory::GetPointer(addr, 16), 0, 16);
        c->token = Memory::RegisterDeferredRamRead(addr, 16, materialize, c);
        check(c->token != 0);
        return c;
    };
    for (uint32_t bank : {0u, 0x10000000u}) {
        for (uint32_t registration : {0u, 0x80000000u, 0xc0000000u}) {
            for (uint32_t access : {0u, 0x80000000u, 0xc0000000u}) {
                auto c = schedule(bank + registration + 0x4000);
                check(*Memory::GetPointer(c->address) == 0 && c->calls == 0);
                check(MemoryInline::FlatRead32(bank + access + 0x4000) == 0x5a5a5a5a);
                for (uint32_t other : {0u, 0x80000000u, 0xc0000000u})
                    check(Memory::Read32(bank + other + 0x4000) == 0x5a5a5a5a);
                check(c->calls == 1 && !Memory::CancelDeferredRead(c->token));
            }
        }
    }
    // Cancellation is selective, releases owned context, and preserves a
    // neighbouring pending range in the same coarse page.
    auto a = schedule(0x80008000), b = schedule(0x80008100);
    std::weak_ptr<Copy> weak = a;
    const auto token = a->token;
    a.reset(); check(!weak.expired());
    check(Memory::CancelDeferredRead(token) && weak.expired());
    check(!Memory::CancelDeferredRead(token));
    check(Memory::Read32(0xc0008000) == 0 && b->calls == 0);
    check(Memory::Read32(0x8100) == 0x5a5a5a5a && b->calls == 1);
    a = schedule(0x80008000); weak = a; a.reset();
    check(Memory::Read32(0xc0008000) == 0x5a5a5a5a && weak.expired());
    // Both failure mechanisms leave guards and ownership intact for retry.
    for (bool throwing : {false, true}) {
        a = schedule(0x80008000); a->failures = 2; a->throwFailure = throwing;
        for (unsigned i = 0; i < 2; ++i) {
            bool failed = false;
            try { (void)MemoryInline::FlatRead32(0xc0008000); } catch (const std::exception&) { failed = true; }
            check(failed && a->calls == i + 1 && *Memory::GetPointer(a->address) == 0);
        }
        check(Memory::Read32(0x8000) == 0x5a5a5a5a && a->calls == 3);
    }
    // Straddling a 1 MiB fast-page boundary must resolve the successor range.
    std::memset(Memory::GetPointer(0x800ffffc, 4), 0x11, 4);
    a = schedule(0x80100000);
    check(Memory::Read64(0xc00ffffc) == 0x111111115a5a5a5aull && a->calls == 1);
    check(MemoryInline::g_fullReadablePageBias[0x800ffffc >> 20] != 0);
    // A partial scalar store must first populate the other bytes of the copy.
    a = schedule(0x80008000);
    MemoryInline::FlatWriteRam32(0xc0008004, 0x12345678);
    check(a->calls == 1 && Memory::Read32(0x8000) == 0x5a5a5a5a);
    check(Memory::Read32(0x8004) == 0x12345678 && Memory::Read32(0x8008) == 0x5a5a5a5a);
    a = schedule(0x80008000);
    MemoryInline::WriteStack16(0x80008002, 0xabcd);
    check(a->calls == 1 && Memory::Read32(0x8000) == 0x5a5aabcd);
    a = schedule(0x80008000);
    check(MemoryInline::ReadStack32(0xc0008000) == 0x5a5a5a5a && a->calls == 1);
    a = schedule(0x80008000); a->checkReentry = true;
    check(Memory::Read32(0x8000) == 0x5a5a5a5a && a->calls == 1);
    a = schedule(0x80008000); weak = a; a.reset();
    Memory::ClearDeferredReads();
    check(weak.expired() && Memory::Read32(0x8000) == 0);
    const auto valid = std::make_shared<Copy>();
    for (uint32_t invalid : {0x40008000u, 0xcc008000u, 0x81800000u, 0x98000000u, 0xfffffff8u})
        check(!Memory::RegisterDeferredRamRead(invalid, 16, materialize, valid));
    check(!Memory::RegisterDeferredRamRead(0x817ffff8, 16, materialize, valid));
    check(!Memory::RegisterDeferredRamRead(0x80008000, SIZE_MAX, materialize, valid));
    check(!Memory::RegisterDeferredRamRead(0x80008000, 0, materialize, valid));
    check(!Memory::RegisterDeferredRamRead(0x80008000, 16, nullptr, valid));
    check(!Memory::RegisterDeferredRamRead(0x80008000, 16, materialize, {}));
    check(!Memory::RegisterDeferredRead(0x80008000, SIZE_MAX, materialize, valid.get()));
    return checks;
}
