// SPDX-License-Identifier: GPL-3.0-only
#include "gpu_fence.h"
#include <cstdio>
#include <fstream>
#include <stdexcept>
using namespace mkw::agc;
static unsigned checks;
static void check(bool v){++checks;if(!v)throw std::runtime_error("GPU completion packet mismatch");}
template<class F>static void rejects(F f){bool caught=false;try{f();}catch(const std::invalid_argument&){caught=true;}check(caught);}
int main(int argc,char** argv){try{
    if(argc!=2)throw std::runtime_error("Expected captured AGC ReleaseMem words");
    std::array<uint32_t,8> captured{};std::ifstream input(argv[1],std::ios::binary);
    input.read(reinterpret_cast<char*>(captured.data()),sizeof captured);check(input.gcount()==sizeof captured);
    check(captured[0]==0xc0064900);check(((captured[2]>>24)&7)==2);check((captured[2]>>29)==2);
    for(uint64_t address:{uint64_t{64},uint64_t{0x12340000},uint64_t{0x123456780000},(uint64_t{1}<<48)-64})
    for(uint64_t serial:{uint64_t{1},uint64_t{0x12345678abcdef01},~uint64_t{0}}){
        auto p=encode_gpu_completion(address,serial);
        check(p[0]==captured[0]&&p[1]==captured[1]);
        check(p[2]==(captured[2]&~0x07000000u)&&p[7]==0);
        check((uint64_t(p[4])<<32|p[3])==address);
        check((uint64_t(p[6])<<32|p[5])==serial);
        check(((p[0]>>16)&0x3fff)+2==p.size());
    }
    for(uint64_t address:{uint64_t{0},uint64_t{8},uint64_t{63},uint64_t{65},uint64_t{1}<<48,~uint64_t{0}})
        rejects([&]{encode_gpu_completion(address,1);});
    rejects([]{encode_gpu_completion(64,0);});
    std::printf("PASS GPU completion: %u checks against captured PS5 AGC packet; host encoding only\n",checks);return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL %s\n",e.what());return 1;}}
