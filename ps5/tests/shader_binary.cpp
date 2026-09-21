// SPDX-License-Identifier: GPL-3.0-only
#include "gpu_shader.h"
#include <fstream>
#include <iterator>
#include <iostream>
#include <cstring>
#include <vector>
#include <stdexcept>
int main(int argc,char** argv){try{
    unsigned checks=0;auto require=[&](bool b){if(!b)throw std::runtime_error("Shader parser expectation failed");++checks;};
    auto put=[](auto& b,size_t off,uint64_t v,unsigned n){for(unsigned i=0;i<n;++i)b.at(off+i)=uint8_t(v>>(8*i));};
    std::vector<uint8_t> fixture(512);std::memcpy(fixture.data(),"\177ELF\2\1\1",7);
    put(fixture,40,64,8);put(fixture,58,64,2);put(fixture,60,4,2);put(fixture,62,1,2);
    const char names[]="\0.shstrtab\0.shader_header\0.shader_text\0";
    std::memcpy(fixture.data()+320,names,sizeof(names));
    put(fixture,128,1,4);put(fixture,132,3,4);put(fixture,152,320,8);put(fixture,160,sizeof(names),8);
    put(fixture,192,11,4);put(fixture,196,1,4);put(fixture,216,384,8);put(fixture,224,96,8);
    put(fixture,256,26,4);put(fixture,260,1,4);put(fixture,280,480,8);put(fixture,288,32,8);
    std::memcpy(fixture.data()+384,"1234",4);fixture[480]=0xa5;
    auto result=mkw::agc::parse_shader_binary(fixture);require(result.header.size()==96&&result.code.size()==32&&result.code[0]==0xa5);
    auto reject=[&](auto b){bool rejected=false;try{(void)mkw::agc::parse_shader_binary(b);}catch(const std::invalid_argument&){rejected=true;}require(rejected);};
    for(auto size:{0,63,319,383,479,511}){auto b=fixture;b.resize(size);reject(b);}
    for(auto mutation:std::vector<std::pair<size_t,uint64_t>>{{40,UINT64_MAX},{216,UINT64_MAX},{224,UINT64_MAX},{280,400}}){auto b=fixture;put(b,mutation.first,mutation.second,8);reject(b);}
    for(auto mutation:std::vector<std::pair<size_t,uint8_t>>{{0,0},{4,1},{5,2},{58,63},{60,0},{62,4},{132,1},{196,8},{192,255},{256,11},{384,0}}){auto b=fixture;b[mutation.first]=mutation.second;reject(b);}
    for(int i=1;i<argc;++i){std::ifstream file(argv[i],std::ios::binary);if(!file)throw std::runtime_error("Missing shader fixture");std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)),{});auto p=mkw::agc::parse_shader_binary(bytes);require(p.header.size()>=96&&!p.code.empty());std::cout<<"Shader "<<i<<": header "<<p.header.size()<<", code "<<p.code.size()<<" bytes\n";}
    std::cout<<"PASS "<<checks<<" bounded shader container checks\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
