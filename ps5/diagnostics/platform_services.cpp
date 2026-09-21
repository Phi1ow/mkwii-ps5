// SPDX-License-Identifier: GPL-3.0-only
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <vector>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <cerrno>
#include <ctime>
#include <fcntl.h>
#include <unistd.h>
#include <thread>
#include <chrono>
#include <cryptopp/hrtimer.h>
extern "C" void mkw_diagnostic_log(const char*);
extern "C" void arc4random_buf(void*,size_t);
extern "C" int mkw_test_native_dns();
extern "C" int mkw_test_addrinfo();
extern "C" int mkw_test_platform_services(){try{
    mkw_diagnostic_log("[mkw-services] begin system random generation via libSceRandom\n");
    unsigned checks=0;
    auto check=[&](bool b){++checks;if(!b){char error[96];std::snprintf(error,sizeof(error),"service check %u failed (errno %d)",checks,errno);throw std::runtime_error(error);}};
    arc4random_buf(nullptr,0);
    for(size_t size:std::array<size_t,8>{1,63,64,65,127,128,129,4097}){
        std::vector<unsigned char> buffer(size+34,0xa5);
        arc4random_buf(buffer.data()+17,size);
        for(unsigned i=0;i<17;++i)check(buffer[i]==0xa5&&buffer[size+17+i]==0xa5);
        // This is an API smoke check, not an assessment of cryptographic quality.
        if(size>=63){std::vector<unsigned char> second(size);arc4random_buf(second.data(),size);
            check(std::memcmp(buffer.data()+17,second.data(),size)!=0);}
    }
    char text[160];std::snprintf(text,sizeof(text),"[mkw-services] PASS %u native random bounds/smoke checks, including unaligned 4097-byte output\n",checks);
    mkw_diagnostic_log(text);
    for(int clockId:std::array<int,4>{CLOCK_VIRTUAL,CLOCK_PROF,CLOCK_THREAD_CPUTIME_ID,CLOCK_PROCESS_CPUTIME_ID}){
        timespec time{};errno=0;const int rc=clock_gettime(clockId,&time);const int error=errno;
        std::snprintf(text,sizeof(text),"[mkw-services] CPU clock=%d rc=%d errno=%d sec=%lld ns=%lld\n",clockId,rc,error,(long long)time.tv_sec,(long long)time.tv_nsec);mkw_diagnostic_log(text);
    }
    in_addr address{};char formatted[INET_ADDRSTRLEN]{};
    check(inet_pton(AF_INET,"192.0.2.17",&address)==1);
    const unsigned char expected[4]={192,0,2,17};check(!std::memcmp(&address,expected,4));
    check(inet_ntop(AF_INET,&address,formatted,sizeof(formatted))==formatted&&!std::strcmp(formatted,"192.0.2.17"));
    auto saved=address;check(inet_pton(AF_INET,"256.0.2.17",&address)==0&&!std::memcmp(&saved,&address,sizeof(address)));
    errno=0;check(inet_ntop(AF_INET,&address,formatted,2)==nullptr&&errno==ENOSPC);
    mkw_diagnostic_log("[mkw-services] PASS native IPv4 conversion, invalid input and short destination\n");
    // dup was independently rejected with EPERM in PPSA99598. This test
    // measures CPU clock semantics; it does not supersede that failed check.
    auto ns=[](int id){timespec value{};if(clock_gettime(id,&value)!=0)throw std::runtime_error("CPU clock read failed");
        return static_cast<long long>(value.tv_sec)*1000000000LL+value.tv_nsec;};
    auto burn=[](){auto end=std::chrono::steady_clock::now()+std::chrono::milliseconds(250);
        while(std::chrono::steady_clock::now()<end){asm volatile("" ::: "memory");}};
    auto t0=ns(CLOCK_THREAD_CPUTIME_ID);burn();auto busy=ns(CLOCK_THREAD_CPUTIME_ID)-t0;
    t0=ns(CLOCK_THREAD_CPUTIME_ID);std::this_thread::sleep_for(std::chrono::milliseconds(300));
    auto asleep=ns(CLOCK_THREAD_CPUTIME_ID)-t0;
    long long workerCpu=0;bool workerOk=false;
    t0=ns(CLOCK_THREAD_CPUTIME_ID);auto p0=ns(CLOCK_PROCESS_CPUTIME_ID);
    std::thread worker([&](){try{auto start=ns(CLOCK_THREAD_CPUTIME_ID);burn();workerCpu=ns(CLOCK_THREAD_CPUTIME_ID)-start;workerOk=true;}catch(...){}});
    worker.join();auto joined=ns(CLOCK_THREAD_CPUTIME_ID)-t0;auto process=ns(CLOCK_PROCESS_CPUTIME_ID)-p0;
    char clocks[256];std::snprintf(clocks,sizeof(clocks),"[mkw-services] clock semantics ns: busy=%lld sleep=%lld join=%lld worker=%lld process=%lld\n",busy,asleep,joined,workerCpu,process);mkw_diagnostic_log(clocks);
    check(workerOk);check(busy>50000000&&busy<1500000000);check(asleep>=0&&asleep<50000000);
    check(joined>=0&&joined<50000000);check(workerCpu>50000000&&workerCpu<1500000000);
    check(process>=workerCpu&&process<workerCpu+500000000);
    CryptoPP::ThreadUserTimer cpuTimer(CryptoPP::TimerBase::MILLISECONDS);
    cpuTimer.StartTimer();burn();const double cpuBusy=cpuTimer.ElapsedTimeAsDouble();
    std::this_thread::sleep_for(std::chrono::milliseconds(300));const double cpuAfterSleep=cpuTimer.ElapsedTimeAsDouble();
    std::snprintf(text,sizeof(text),"[mkw-services] actual Crypto++ ThreadUserTimer ms: busy=%.3f after-sleep=%.3f\n",cpuBusy,cpuAfterSleep);mkw_diagnostic_log(text);
    check(cpuBusy>50&&cpuBusy<1500);check(cpuAfterSleep>=cpuBusy&&cpuAfterSleep-cpuBusy<50);
    CryptoPP::Timer wallTimer(CryptoPP::TimerBase::MILLISECONDS);wallTimer.StartTimer();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));auto wallSleep=wallTimer.ElapsedTimeAsDouble();
    check(wallSleep>=75&&wallSleep<2000);
    std::snprintf(text,sizeof(text),"[mkw-services] PASS %u service checks; thread CPU excludes sleep and another thread's work\n",checks);mkw_diagnostic_log(text);
    if(mkw_test_addrinfo())return 1;
    return mkw_test_native_dns();
}catch(const std::exception& e){mkw_diagnostic_log("[mkw-services] FAIL ");mkw_diagnostic_log(e.what());mkw_diagnostic_log("\n");return 1;}}
