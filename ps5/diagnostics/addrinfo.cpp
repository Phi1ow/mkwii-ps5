// SPDX-License-Identifier: GPL-3.0-only
#include <sys/types.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <thread>
#include <array>
extern "C" int mkw_ps5_resolve_ipv4(const char*,uint32_t*);
extern "C" void mkw_diagnostic_log(const char*);
extern "C" int mkw_test_addrinfo() {try {
    unsigned checks=0;
    auto check=[&](bool ok){++checks;if(!ok)throw std::runtime_error("addrinfo check "+std::to_string(checks));};
    struct List {addrinfo* value=nullptr;~List(){freeaddrinfo(value);}};
    for (unsigned port : std::array<unsigned,7>{0,1,53,80,443,32768,65535}) {
        for (int type : std::array<int,3>{0,SOCK_STREAM,SOCK_DGRAM}) {
            addrinfo hints{};hints.ai_family=AF_INET;hints.ai_socktype=type;hints.ai_flags=AI_NUMERICHOST|AI_NUMERICSERV;
            List list;auto text=std::to_string(port);
            check(getaddrinfo("192.0.2.17",text.c_str(),&hints,&list.value)==0);unsigned count=0;
            for (auto* entry=list.value;entry;entry=entry->ai_next) {
                check(++count<=2);check(entry->ai_family==AF_INET&&entry->ai_addrlen==sizeof(sockaddr_in));
                check(entry->ai_socktype==SOCK_STREAM||entry->ai_socktype==SOCK_DGRAM);
                check(entry->ai_protocol==(entry->ai_socktype==SOCK_STREAM?IPPROTO_TCP:IPPROTO_UDP));
                check(entry->ai_canonname==nullptr);
                auto* address=reinterpret_cast<sockaddr_in*>(entry->ai_addr);
                check(address->sin_len==sizeof(sockaddr_in)&&address->sin_family==AF_INET);
                check(ntohs(address->sin_port)==port&&address->sin_addr.s_addr==htonl(0xc0000211u));
                if(type)check(entry->ai_socktype==type);
            }
            check(count==(type?1u:2u));
        }
    }
    for (int passive : {0,AI_PASSIVE}) {
        addrinfo hints{};hints.ai_flags=passive;hints.ai_protocol=IPPROTO_UDP;
        List list;check(getaddrinfo(nullptr,"53",&hints,&list.value)==0);
        check(list.value&&!list.value->ai_next&&list.value->ai_socktype==SOCK_DGRAM);
        auto* address=reinterpret_cast<sockaddr_in*>(list.value->ai_addr);
        check(address->sin_addr.s_addr==htonl(passive?INADDR_ANY:INADDR_LOOPBACK));
    }
    auto fail=[&](const char* node,const char* service,addrinfo hints,int expected) {
        auto* result=reinterpret_cast<addrinfo*>(uintptr_t(1));
        check(getaddrinfo(node,service,&hints,&result)==expected);check(result==nullptr);
    };
    addrinfo hints{};hints.ai_flags=AI_NUMERICHOST;
    fail("256.0.2.17","80",hints,EAI_NONAME);fail("example.com","80",hints,EAI_NONAME);
    fail("","80",hints,EAI_NONAME);fail(nullptr,nullptr,hints,EAI_NONAME);
    std::string longName(256,'a');fail(longName.c_str(),nullptr,hints,EAI_NONAME);
    for (const char* service : {"", "-1", "+80", "80x", "65536", "99999999999999999999", "http"})
        fail("192.0.2.17",service,hints,EAI_SERVICE);
    hints={};hints.ai_flags=AI_CANONNAME;fail("192.0.2.17",nullptr,hints,EAI_BADFLAGS);
    hints={};hints.ai_family=AF_INET6;fail("::1",nullptr,hints,EAI_FAMILY);
    hints={};hints.ai_socktype=SOCK_RAW;fail("192.0.2.17",nullptr,hints,EAI_SOCKTYPE);
    hints={};hints.ai_protocol=IPPROTO_ICMP;fail("192.0.2.17",nullptr,hints,EAI_PROTOCOL);
    hints={};hints.ai_socktype=SOCK_STREAM;hints.ai_protocol=IPPROTO_UDP;fail("192.0.2.17",nullptr,hints,EAI_BADHINTS);
    errno=0;check(getaddrinfo("192.0.2.17",nullptr,nullptr,nullptr)==EAI_SYSTEM&&errno==EINVAL);
    freeaddrinfo(nullptr);
    // Exercise real module initialization and independent native pools from
    // parallel workers. Numeric native queries do not need public DNS.
    std::array<int,3> results{};std::array<uint32_t,3> addresses{};
    std::array<std::thread,3> workers;
    for(unsigned i=0;i<workers.size();++i) workers[i]=std::thread([&,i]{results[i]=mkw_ps5_resolve_ipv4("192.0.2.17",&addresses[i]);});
    for(auto& worker:workers)worker.join();
    for(unsigned i=0;i<results.size();++i)check(results[i]==0&&addresses[i]==htonl(0xc0000211u));
    char log[160];std::snprintf(log,sizeof(log),"[mkw-addrinfo] PASS %u checks: IPv4 lists, ports, protocol hints, failure outputs, concurrent native pools\n",checks);mkw_diagnostic_log(log);
    return 0;
}catch(const std::exception& e){mkw_diagnostic_log("[mkw-addrinfo] FAIL ");mkw_diagnostic_log(e.what());mkw_diagnostic_log("\n");return 1;}}
