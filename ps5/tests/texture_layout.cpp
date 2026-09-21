// SPDX-License-Identifier: GPL-3.0-only
#include "texture_layout.h"
#include <cstdio>
#include <vector>
#include <stdexcept>
#include <cstring>
using mkw::agc::TextureLayout;
template<class T>T read(std::FILE* f) { T value;if(std::fread(&value,sizeof(value),1,f)!=1)throw std::runtime_error("Truncated reference");return value; }
void read_bytes(std::FILE* f,std::vector<uint8_t>& bytes) { if(std::fread(bytes.data(),1,bytes.size(),f)!=bytes.size())throw std::runtime_error("Truncated pixels"); }
template<class F>void rejected(F&& f) { bool threw=false;try{f();}catch(const std::exception&){threw=true;}if(!threw)throw std::runtime_error("Invalid texture input accepted"); }
int main(int argc,char** argv) {
    if(argc!=2)return 2;
    std::FILE* f=std::fopen(argv[1],"rb");if(!f)return 2;
    try {
        uint32_t count=read<uint32_t>(f);
        for(uint32_t i=0;i<count;++i) {
            auto w=read<uint32_t>(f),h=read<uint32_t>(f),levels=read<uint32_t>(f);
            size_t gpuBytes=read<uint64_t>(f),linearBytes=read<uint64_t>(f);
            std::array<uint32_t,8> descriptor{};for(auto& d:descriptor)d=read<uint32_t>(f);
            std::vector<uint8_t> source(linearBytes),expected(gpuBytes),actual(gpuBytes+64,0xa5);
            read_bytes(f,source);read_bytes(f,expected);
            TextureLayout layout(w,h,levels);
            if(layout.byte_size()!=gpuBytes || layout.linear_byte_size()!=linearBytes || layout.descriptor(0x1234560000)!=descriptor)
                throw std::runtime_error("Layout or descriptor differs from SharpProspero");
            layout.tile(actual,source);
            if(std::memcmp(actual.data(),expected.data(),gpuBytes)) {
                std::fprintf(stderr,"Pixel difference for %ux%u with %u mips\n",w,h,levels);
                throw std::runtime_error("Tiled pixel data differs from SharpProspero");
            }
            for(size_t j=gpuBytes;j<actual.size();++j)if(actual[j]!=0xa5)throw std::runtime_error("Texture output overrun");
            rejected([&]{layout.pixel_offset(w,0,0);});rejected([&]{layout.pixel_offset(0,h,0);});
            rejected([&]{layout.mip(levels);});rejected([&]{layout.descriptor(0x10001);});
            rejected([&]{layout.descriptor(uint64_t{1}<<48);});
            auto saved=actual;
            rejected([&]{layout.tile(std::span(actual).first(gpuBytes-1),source);});
            rejected([&]{layout.tile(actual,std::span(source).first(linearBytes-1));});
            rejected([&]{layout.tile(actual,std::span(actual).first(linearBytes));});
            if(saved!=actual)throw std::runtime_error("Invalid upload modified destination");
        }
        if(std::fgetc(f)!=EOF)throw std::runtime_error("Unexpected reference tail");
        rejected([]{TextureLayout(0,1,1);});rejected([]{TextureLayout(1,0,1);});
        rejected([]{TextureLayout(16385,1,1);});rejected([]{TextureLayout(1,1,2);});
        rejected([]{TextureLayout(1,1,0);});
        std::printf("PASS %u full SharpProspero differential cases, mip chains, descriptors, bounds and output guards\n",count);
        std::fclose(f);return 0;
    }catch(const std::exception& e){std::fprintf(stderr,"FAIL %s\n",e.what());std::fclose(f);return 1;}
}
