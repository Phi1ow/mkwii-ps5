// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cpuid.h>
#include <cstddef>
#include <cstdint>
namespace mkw::agc {
// CPUID.(EAX=7,ECX=0):EBX[23]. Zen 2 implements it; checked once so an older
// host (PC tests) keeps the plain CLFLUSH path.
inline bool cpu_has_clflushopt() noexcept{
    static const bool supported=[]{unsigned a,b,c,d;return __get_cpuid_count(7,0,&a,&b,&c,&d)&&(b&(1u<<23));}();
    return supported;
}
namespace detail {
// CLFLUSH executes in order; CLFLUSHOPT lets the line write-backs overlap and
// is only ordered by the fence that follows (flush_cpu_cache_lines).
__attribute__((target("clflushopt"))) inline void clflushopt_lines(uintptr_t line,uintptr_t end) noexcept{
    for(;line<end;line+=64)__builtin_ia32_clflushopt(reinterpret_cast<const void*>(line));
}
}
// Writes back and invalidates every 64-byte line intersecting [data, data+bytes),
// then fences (MFENCE), so the GPU sees CPU writes and later CPU reads refetch
// GPU writes. Same contract as the former per-call-site CLFLUSH loops.
inline void flush_cpu_cache_lines(const void* data,size_t bytes) noexcept{
    const uintptr_t begin=reinterpret_cast<uintptr_t>(data);
    uintptr_t line=begin&~uintptr_t(63);const uintptr_t end=begin+bytes;
    if(cpu_has_clflushopt())detail::clflushopt_lines(line,end);
    else for(;line<end;line+=64)__builtin_ia32_clflush(reinterpret_cast<const void*>(line));
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
}
}
