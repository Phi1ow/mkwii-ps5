#include "guest_memory_ps5.h"
using Size = MkwPs5Memory::Size;
extern "C" {
int sceKernelReserveVirtualRange(void**, Size, int, Size);
Size sceKernelGetDirectMemorySize();
int sceKernelAllocateDirectMemory(long long, long long, Size, Size, int, long long*);
int sceKernelMapDirectMemory(void**, Size, int, int, long long, Size);
int sceKernelReleaseDirectMemory(long long, Size);
int sceKernelMunmap(void*, Size);
}
namespace MkwPs5Memory {
namespace {
constexpr int Invalid = -1;
constexpr int Busy = -2;
bool Aligned(Size v) { return (v & (PageSize - 1)) == 0; }
}
int Reserve(Arena& arena) {
    if (arena.base) return Busy;
    void* address = reinterpret_cast<void*>(GuestBase);
    // Fixed + no-overwrite: never replace an unrelated mapping at this address.
    int rc = sceKernelReserveVirtualRange(&address, GuestSpaceSize, 0x90, PageSize);
    if (rc) return rc;
    if (address != reinterpret_cast<void*>(GuestBase)) {
        sceKernelMunmap(address, GuestSpaceSize);
        return Invalid;
    }
    arena.base = address; arena.size = GuestSpaceSize;
    return 0;
}
int OpenBacking(Backing& backing, Size bytes) {
    if (backing.physical >= 0 || backing.host) return Busy;
    if (!bytes || bytes > static_cast<Size>(-1) - PageSize + 1) return Invalid;
    Size rounded = (bytes + PageSize - 1) & ~(PageSize - 1);
    Size poolSize = sceKernelGetDirectMemorySize();
    if (rounded > poolSize || poolSize > 0x7fffffffffffffffull) return Invalid;
    long long physical = -1;
    int rc = sceKernelAllocateDirectMemory(0, static_cast<long long>(poolSize),
        rounded, PageSize, 11 /* cached CPU memory */, &physical);
    if (rc) return rc;
    backing.physical = physical; backing.size = rounded;
    rc = sceKernelMapDirectMemory(&backing.host, rounded, 3 /* CPU RW */, 0, physical, PageSize);
    if (rc) { CloseBacking(backing); return rc; }
    return 0;
}
int MapAlias(Alias& alias, Arena& arena, Size guestOffset,
             Backing& backing, Size backingOffset, Size bytes) {
    if (alias.address) return Busy;
    if (!arena.base || !backing.host || !bytes || !Aligned(guestOffset) ||
        !Aligned(backingOffset) || !Aligned(bytes) || guestOffset > arena.size ||
        bytes > arena.size - guestOffset || backingOffset > backing.size ||
        bytes > backing.size - backingOffset) return Invalid;
    Size begin = reinterpret_cast<Size>(arena.base) + guestOffset;
    Size end = begin + bytes;
    for (Alias* other = arena.aliases; other; other = other->next) {
        Size otherBegin = reinterpret_cast<Size>(other->address);
        if (begin < otherBegin + other->size && otherBegin < end) return Busy;
    }
    void* address = reinterpret_cast<void*>(begin);
    // The interval is inside our own reservation and contains no existing alias.
    int rc = sceKernelMapDirectMemory(&address, bytes, 3, 0x10 /* fixed */,
        backing.physical + static_cast<long long>(backingOffset), PageSize);
    if (rc) return rc;
    if (address != reinterpret_cast<void*>(begin)) {
        sceKernelMunmap(address, bytes); return Invalid;
    }
    alias.address = address; alias.size = bytes;
    alias.arena = &arena; alias.backing = &backing;
    alias.next = arena.aliases; arena.aliases = &alias;
    ++backing.aliases;
    return 0;
}
int UnmapAlias(Alias& alias) {
    if (!alias.address) return 0;
    // Replace the owned view with a reservation atomically. A bare munmap
    // would leave a hole another allocator could occupy before CloseArena.
    void* address = alias.address;
    int rc = sceKernelReserveVirtualRange(&address, alias.size, 0x10, PageSize);
    if (rc) return rc;
    Alias** link = &alias.arena->aliases;
    while (*link && *link != &alias) link = &(*link)->next;
    if (*link) *link = alias.next;
    --alias.backing->aliases;
    alias.address = nullptr; alias.size = 0;
    alias.arena = nullptr; alias.backing = nullptr; alias.next = nullptr;
    return 0;
}
int CloseBacking(Backing& backing) {
    if (backing.aliases) return Busy;
    if (backing.host) {
        int rc = sceKernelMunmap(backing.host, backing.size);
        if (rc) return rc;
        backing.host = nullptr;
    }
    if (backing.physical >= 0) {
        int rc = sceKernelReleaseDirectMemory(backing.physical, backing.size);
        if (rc) return rc;
        backing.physical = -1; backing.size = 0;
    }
    return 0;
}
int CloseArena(Arena& arena) {
    if (arena.aliases) return Busy;
    if (!arena.base) return 0;
    int rc = sceKernelMunmap(arena.base, arena.size);
    if (!rc) { arena.base = nullptr; arena.size = 0; }
    return rc;
}
}
