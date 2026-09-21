// SPDX-License-Identifier: GPL-3.0-only
#include <netdb.h>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <atomic>
extern "C" int sceSysmoduleLoadModuleInternal(uint32_t);
extern "C" int sceNetInit();
extern "C" int sceNetPoolCreate(const char*,int,int);
extern "C" int sceNetPoolDestroy(int);
extern "C" int sceNetResolverCreate(const char*,int,int);
extern "C" int sceNetResolverDestroy(int);
extern "C" int sceNetResolverStartNtoa(int,const char*,uint32_t*,int,int,int);
namespace {
std::once_flag ready;
std::atomic<unsigned> sequence{0};
thread_local int lastNativeResult=0;
void initialize() {
    // SharpProspero PayloadSysmoduleMap; native load/init validated PPSA99601.
    lastNativeResult=sceSysmoduleLoadModuleInternal(0x80000009u);
    if (lastNativeResult<0) throw 1;
    // Another subsystem may own a previous initialization. The real pool
    // creation below determines usability; never terminate shared Net here.
    sceNetInit();
}
}
extern "C" int mkw_ps5_dns_last_native_result() {return lastNativeResult;}
extern "C" int mkw_ps5_resolve_ipv4(const char* name,uint32_t* address) {
    lastNativeResult=0;
    if(!name||!address){errno=EINVAL;return EAI_SYSTEM;}
    try {std::call_once(ready,initialize);} catch (...) {errno=EIO;return EAI_SYSTEM;}
    char label[32];std::snprintf(label,sizeof(label),"mkw_dns_%u",sequence.fetch_add(1,std::memory_order_relaxed));
    const int pool=sceNetPoolCreate(label,0x4000,0);
    if (pool<0) {lastNativeResult=pool;errno=EIO;return EAI_SYSTEM;}
    const int resolver=sceNetResolverCreate(label,pool,0);
    if (resolver<0) {lastNativeResult=resolver;sceNetPoolDestroy(pool);errno=EIO;return EAI_SYSTEM;}
    uint32_t resolved=0;
    // This call only occurs on WiiCompiled's deferred DNS worker. These are
    // SharpProspero's documented timeout/retry arguments, not a proved wall
    // deadline. Never invoke it from the rendering or guest CPU thread.
    const int rc=sceNetResolverStartNtoa(resolver,name,&resolved,1000000,0,0);
    lastNativeResult=rc;
    const int resolverCleanup=sceNetResolverDestroy(resolver);
    const int poolCleanup=sceNetPoolDestroy(pool);
    if (resolverCleanup<0||poolCleanup<0) {errno=EIO;return EAI_SYSTEM;}
    // No verified mapping of PS5 resolver codes to POSIX EAI_* yet. Preserve
    // failure without claiming NXDOMAIN, retryability, or a valid address.
    if (rc<0) return EAI_FAIL;
    *address=resolved;return 0;
}
