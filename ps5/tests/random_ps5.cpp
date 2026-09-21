// SPDX-License-Identifier: GPL-3.0-only
#include <array>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <vector>
#include <cstdint>
extern "C" void arc4random_buf(void*,size_t);
namespace {
unsigned checks=0,calls=0,failAt=0;
unsigned loads=0;
int loadError=-3;
std::vector<size_t> lengths;
void check(bool yes) { ++checks;if(!yes)throw std::runtime_error("random adapter check"); }
}
extern "C" int sceSysmoduleLoadModule(unsigned short id) { check(id==0x00ba);++loads;return loadError; }
extern "C" int sceRandomGetRandomNumber(void* output,size_t length) {
    check(length>0&&length<=64);lengths.push_back(length);++calls;
    if(calls==failAt)return int(0x80020005u);
    std::memset(output,int(calls),length);return 0;
}
int main(){try{
    arc4random_buf(nullptr,0);check(calls==0);
    std::array<uint8_t,2> failedLoad{0xa5,0xa5};bool loadFailed=false;
    try{arc4random_buf(failedLoad.data(),failedLoad.size());}catch(const std::runtime_error&){loadFailed=true;}
    check(loadFailed&&loads==1&&calls==0&&failedLoad==std::array<uint8_t,2>{});loadError=0;
    for(size_t size:std::array<size_t,8>{1,63,64,65,127,128,129,4097}){
        std::vector<uint8_t> bytes(size+34,0xa5);calls=0;lengths.clear();
        arc4random_buf(bytes.data()+17,size);
        check(calls==(size+63)/64);
        for(size_t i=0;i<17;++i)check(bytes[i]==0xa5&&bytes[size+17+i]==0xa5);
        for(size_t i=0;i<size;++i)check(bytes[i+17]==uint8_t(i/64+1));
    }
    check(loads==2); // Failed call_once retried once; successful load reused.
    std::array<uint8_t,132> bytes;bytes.fill(0xa5);calls=0;failAt=2;
    bool failed=false;try{arc4random_buf(bytes.data()+1,130);}catch(const std::runtime_error&){failed=true;}
    check(failed&&calls==2&&bytes.front()==0xa5&&bytes.back()==0xa5);
    for(size_t i=1;i<131;++i)check(bytes[i]==0);
    failed=false;try{arc4random_buf(nullptr,1);}catch(const std::invalid_argument&){failed=true;}check(failed);
    failed=false;try{arc4random_buf(reinterpret_cast<void*>(UINTPTR_MAX-2),4);}catch(const std::invalid_argument&){failed=true;}check(failed);
    std::printf("PASS PS5 random adapter: %u checks (chunk bounds, exact coverage, canaries, failure wipe)\n",checks);return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL random adapter at check %u: %s\n",checks,e.what());return 1;}}
