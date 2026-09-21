#pragma once
namespace MkwPs5Memory {
using Size = decltype(sizeof(0));
constexpr Size PageSize = 16384;
constexpr Size GuestSpaceSize = 0x100000000ull;
// Within SharpProspero KernelMemoryQuery.MapAreaStart..MapAreaEnd.
constexpr Size GuestBase = 0x2000000000ull;
struct Alias;
struct Arena {
    void* base = nullptr;
    Size size = 0;
    Alias* aliases = nullptr;
    Arena() = default;
    Arena(const Arena&) = delete;
    Arena& operator=(const Arena&) = delete;
};
struct Backing {
    long long physical = -1;
    Size size = 0;
    void* host = nullptr;
    Size aliases = 0;
    Backing() = default;
    Backing(const Backing&) = delete;
    Backing& operator=(const Backing&) = delete;
};
struct Alias {
    void* address = nullptr;
    Size size = 0;
    Arena* arena = nullptr;
    Backing* backing = nullptr;
    Alias* next = nullptr;
    Alias() = default;
    Alias(const Alias&) = delete;
    Alias& operator=(const Alias&) = delete;
};
// Explicit lifetimes: unmap aliases before closing their arena or backing.
// Handles must stay at stable addresses. API is confined to the runtime thread.
int Reserve(Arena& arena);
int OpenBacking(Backing& backing, Size bytes);
int MapAlias(Alias& alias, Arena& arena, Size guestOffset,
             Backing& backing, Size backingOffset, Size bytes);
int UnmapAlias(Alias& alias);
int CloseBacking(Backing& backing);
int CloseArena(Arena& arena);
}
