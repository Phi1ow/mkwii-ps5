// SPDX-License-Identifier: GPL-3.0-only
// WiiCompiled's deferred network worker consumes IPv4 sockaddr payloads.
// Supported: AF_INET/AF_UNSPEC, TCP/UDP, numeric services, passive and
// numeric-only queries. Unsupported families, service names and flags fail
// explicitly; no fabricated DNS answers or success with an empty list.
#include <sys/types.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <cerrno>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <initializer_list>
extern "C" int mkw_ps5_resolve_ipv4(const char*, uint32_t*);

namespace {
struct Result { addrinfo info; sockaddr_in address; };
static_assert(offsetof(Result, info) == 0);
int service_port(const char* service, uint16_t& port) {
    if (!service) {port=0;return 0;}
    if (!*service) return EAI_SERVICE;
    unsigned number=0;
    for (const unsigned char* p=reinterpret_cast<const unsigned char*>(service);*p;++p) {
        if (*p<'0'||*p>'9') return EAI_SERVICE;
        number=number*10+(*p-'0');
        if (number>65535) return EAI_SERVICE;
    }
    port=htons(static_cast<uint16_t>(number));return 0;
}
}

extern "C" void freeaddrinfo(addrinfo* list) {
    while (list) {auto* next=list->ai_next;std::free(list);list=next;}
}

extern "C" int getaddrinfo(const char* node, const char* service,
                          const addrinfo* hints, addrinfo** result) {
    if (!result) {errno=EINVAL;return EAI_SYSTEM;}
    *result=nullptr;
    if (!node&&!service) return EAI_NONAME;
    const int flags=hints?hints->ai_flags:0;
    const int family=hints?hints->ai_family:AF_UNSPEC;
    int type=hints?hints->ai_socktype:0;
    int protocol=hints?hints->ai_protocol:0;
    if (flags&~(AI_PASSIVE|AI_NUMERICHOST|AI_NUMERICSERV)) return EAI_BADFLAGS;
    if (family!=AF_UNSPEC&&family!=AF_INET) return EAI_FAMILY;
    if (type!=0&&type!=SOCK_STREAM&&type!=SOCK_DGRAM) return EAI_SOCKTYPE;
    if (protocol!=0&&protocol!=IPPROTO_TCP&&protocol!=IPPROTO_UDP) return EAI_PROTOCOL;
    if ((type==SOCK_STREAM&&protocol==IPPROTO_UDP)||(type==SOCK_DGRAM&&protocol==IPPROTO_TCP)) return EAI_BADHINTS;
    if (!type&&protocol) type=protocol==IPPROTO_TCP?SOCK_STREAM:SOCK_DGRAM;
    uint16_t port=0;const int serviceError=service_port(service,port);
    if (serviceError) return serviceError;
    in_addr address{};
    if (!node) address.s_addr=htonl(flags&AI_PASSIVE?INADDR_ANY:INADDR_LOOPBACK);
    else {
        if (!*node||std::strlen(node)>255) return EAI_NONAME;
        const int numeric=inet_pton(AF_INET,node,&address);
        if (numeric<0) return EAI_SYSTEM;
        if (numeric==0) {
            if (flags&AI_NUMERICHOST) return EAI_NONAME;
            const int error=mkw_ps5_resolve_ipv4(node,&address.s_addr);
            if (error) return error;
        }
    }
    addrinfo** tail=result;
    for (int candidate : {SOCK_STREAM,SOCK_DGRAM}) {
        if (type&&type!=candidate) continue;
        auto* item=static_cast<Result*>(std::calloc(1,sizeof(Result)));
        if (!item) {freeaddrinfo(*result);*result=nullptr;return EAI_MEMORY;}
        item->address.sin_len=sizeof(sockaddr_in);
        item->address.sin_family=AF_INET;item->address.sin_port=port;item->address.sin_addr=address;
        item->info.ai_flags=flags;item->info.ai_family=AF_INET;
        item->info.ai_socktype=candidate;item->info.ai_protocol=candidate==SOCK_STREAM?IPPROTO_TCP:IPPROTO_UDP;
        item->info.ai_addrlen=sizeof(sockaddr_in);item->info.ai_addr=reinterpret_cast<sockaddr*>(&item->address);
        *tail=&item->info;tail=&item->info.ai_next;
    }
    return 0;
}
