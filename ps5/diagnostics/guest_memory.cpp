#include "guest_memory_ps5.h"
extern "C" void __prospero_klog(const char*);
using namespace MkwPs5Memory;
static void report(const char* stage, int rc) {
    __prospero_klog(stage);
    char hex[12] = {'0','x'};
    for (unsigned i = 0; i < 8; ++i) hex[2+i] = "0123456789abcdef"[(static_cast<unsigned>(rc) >> (28 - i*4)) & 15];
    hex[10] = '\n'; hex[11] = 0; __prospero_klog(hex);
}
static int full_ram_test() {
    Arena arena;
    Backing mem1, mem2;
    Alias views[6];
    const Size offsets[6] = {0, 0x80000000, 0xc0000000, 0x10000000, 0x90000000, 0xd0000000};
    int result = Reserve(arena);
    if (!result) result = OpenBacking(mem1, 24 * 1024 * 1024);
    // WiiCompiled uses NDEV-sized MEM2 (memory.h), not retail Wii's 64 MiB.
    if (!result) result = OpenBacking(mem2, 128 * 1024 * 1024);
    for (unsigned i = 0; !result && i < 6; ++i) {
        Backing& backing = i < 3 ? mem1 : mem2;
        result = MapAlias(views[i], arena, offsets[i], backing, 0, backing.size);
    }
    if (!result) {
        for (unsigned region = 0; region < 2; ++region) {
            Backing& backing = region == 0 ? mem1 : mem2;
            auto* host = static_cast<volatile unsigned*>(backing.host);
            for (Size word = 0; word < backing.size / 4; ++word)
                host[word] = static_cast<unsigned>(word) ^ (region ? 0xfeed1234u : 0xabcd5678u);
            for (unsigned view = region * 3; view < region * 3 + 3; ++view) {
                auto* alias = static_cast<volatile unsigned*>(views[view].address);
                for (Size word = 0; word < backing.size / 4; ++word)
                    if (alias[word] != host[word]) { result = 9; break; }
            }
        }
        // Replacing an alias must retain its reservation and data, repeatedly.
        for (unsigned iteration = 0; !result && iteration < 100; ++iteration) {
            result = UnmapAlias(views[4]);
            if (!result) result = MapAlias(views[4], arena, offsets[4], mem2, 0, mem2.size);
            if (!result) {
                auto* alias = static_cast<volatile unsigned*>(views[4].address);
                alias[iteration * 1024] = iteration;
                if (static_cast<volatile unsigned*>(mem2.host)[iteration * 1024] != iteration) result = 10;
            }
        }
    }
    for (unsigned i = 6; i-- > 0;) if (UnmapAlias(views[i])) result = 11;
    if (CloseBacking(mem2)) result = 12;
    if (CloseBacking(mem1)) result = 12;
    if (CloseArena(arena)) result = 13;
    return result;
}
extern "C" int main() {
    __prospero_klog("[mkw-memory] begin 4 GiB reservation and shared RAM views\n");
    Arena arena; Backing ram; Alias physical, cached, uncached;
    int result = 0;
    int rc = Reserve(arena);
    if (rc) { report("[mkw-memory] FAIL reserve ", rc); return 1; }
    rc = OpenBacking(ram, 65536);
    if (rc) { report("[mkw-memory] FAIL backing ", rc); CloseArena(arena); return 2; }
    if ((rc = MapAlias(physical, arena, 0, ram, 0, ram.size)) ||
        (rc = MapAlias(cached, arena, 0x80000000, ram, 0, ram.size)) ||
        (rc = MapAlias(uncached, arena, 0xc0000000, ram, 0, ram.size))) {
        report("[mkw-memory] FAIL alias ", rc); result = 3;
    } else {
        auto* host = static_cast<volatile unsigned*>(ram.host);
        auto* a = static_cast<volatile unsigned*>(physical.address);
        auto* b = static_cast<volatile unsigned*>(cached.address);
        auto* c = static_cast<volatile unsigned*>(uncached.address);
        for (unsigned i = 0; i < ram.size / sizeof(unsigned); ++i) host[i] = i ^ 0x55aa33cc;
        for (unsigned i = 0; i < ram.size / sizeof(unsigned); ++i)
            if (a[i] != host[i] || b[i] != host[i] || c[i] != host[i]) result = 4;
        b[1024] = 0x12345678; // a separate 4 KiB Wii page in the same 16 KiB PS5 page
        c[3072] = 0x87654321;
        if (host[1024] != 0x12345678 || a[3072] != 0x87654321) result = 5;
        Alias rejected;
        if (MapAlias(rejected, arena, 0x80000000, ram, 0, PageSize) == 0 ||
            MapAlias(rejected, arena, GuestSpaceSize, ram, 0, PageSize) == 0 ||
            MapAlias(rejected, arena, 4096, ram, 0, PageSize) == 0 ||
            CloseBacking(ram) == 0 || CloseArena(arena) == 0) result = 6;
    }
    if (UnmapAlias(uncached)) result = 7;
    if (UnmapAlias(cached)) result = 7;
    if (UnmapAlias(physical)) result = 7;
    if (CloseBacking(ram)) result = 8;
    if (CloseArena(arena)) result = 8;
    if (result) report("[mkw-memory] FAIL validation ", result);
    else __prospero_klog("[mkw-memory] PASS 4 GiB at 0x2000000000; physical/cached/uncached aliases coherent; cleanup complete\n");
    if (!result) {
        result = full_ram_test();
        if (result) report("[mkw-memory] FAIL full RAM ", result);
        else __prospero_klog("[mkw-memory] PASS MEM1 24 MiB + MEM2 128 MiB (WiiCompiled NDEV); all six views; 100 alias remaps; cleanup complete\n");
    }
    return result;
}
