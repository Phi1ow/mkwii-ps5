// SPDX-License-Identifier: GPL-3.0-only
// Native resolver ABI from SharpProspero Interop/Net and PayloadSysmoduleMap.
#include <cstdio>
#include <cstdint>
#include <initializer_list>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netdb.h>
#include <chrono>
extern "C" int sceNetCtlInit();
extern "C" int sceNetCtlGetState(int*);
extern "C" int sceNetCtlGetInfo(int,void*);
extern "C" void sceNetCtlTerm();
extern "C" int mkw_ps5_dns_last_native_result();
extern "C" int sceSysmoduleLoadModuleInternal(uint32_t);
extern "C" int sceNetInit();
extern "C" int sceNetPoolCreate(const char*, int, int);
extern "C" int sceNetPoolDestroy(int);
extern "C" int sceNetResolverCreate(const char*, int, int);
extern "C" int sceNetResolverDestroy(int);
extern "C" int sceNetResolverStartNtoa(int, const char*, uint32_t*, int, int, int);
extern "C" int* sceNetErrnoLoc();
extern "C" void mkw_diagnostic_log(const char*);

extern "C" int mkw_test_native_dns() {
    char log[256];
    int rc=sceSysmoduleLoadModuleInternal(0x80000014u);
    std::snprintf(log,sizeof(log),"[mkw-dns] NetCtl module=0x%08x\n",unsigned(rc));mkw_diagnostic_log(log);
    if(rc>=0){
        rc=sceNetCtlInit();std::snprintf(log,sizeof(log),"[mkw-dns] NetCtl init=0x%08x\n",unsigned(rc));mkw_diagnostic_log(log);
        if(rc==0){
            int state=-1;rc=sceNetCtlGetState(&state);
            std::snprintf(log,sizeof(log),"[mkw-dns] NetCtl state=%d rc=0x%08x\n",state,unsigned(rc));mkw_diagnostic_log(log);
            // Read only the address, route and DNS; no SSID or other profiles.
            for(int code:{14,16,17,18}){alignas(8) char info[256]{};rc=sceNetCtlGetInfo(code,info);info[255]=0;
                std::snprintf(log,sizeof(log),"[mkw-dns] NetCtl field=%d rc=0x%08x value=%.64s\n",code,unsigned(rc),info);mkw_diagnostic_log(log);}
            sceNetCtlTerm();
        }
    }
    addrinfo hints{};hints.ai_family=AF_INET;hints.ai_socktype=SOCK_STREAM;
    addrinfo* results=nullptr;auto start=std::chrono::steady_clock::now();
    rc=getaddrinfo("example.com","443",&hints,&results);
    auto elapsed=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count();
    std::snprintf(log,sizeof(log),"[mkw-dns] adapter public query rc=%d native=0x%08x elapsed-ms=%lld results=%s\n",rc,unsigned(mkw_ps5_dns_last_native_result()),(long long)elapsed,results?"present":"null");mkw_diagnostic_log(log);
    if(rc==0){
        const bool valid=results&&results->ai_addr&&results->ai_family==AF_INET;
        freeaddrinfo(results);
        mkw_diagnostic_log(valid?"[mkw-dns] public name resolved through actual adapter\n":"[mkw-dns] FAIL empty successful result\n");
        return valid?0:1;
    }
    const bool propagated=results==nullptr&&(rc==EAI_FAIL||rc==EAI_SYSTEM);
    freeaddrinfo(results);
    mkw_diagnostic_log(propagated?"[mkw-dns] failure propagated with null result; public DNS remains unvalidated\n":"[mkw-dns] FAIL error output contract\n");
    return propagated?0:1;
}
