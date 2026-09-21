// SPDX-License-Identifier: GPL-3.0-only
#include "gpu_texture.h"
#include <malloc.h>
#include <vector>
#include <cstdio>
#include <stdexcept>
namespace {
bool failAllocate=false,failMap=false,failUnmap=false,failRelease=false,physical=false;
void* mapped=nullptr;
unsigned allocCalls=0,mapCalls=0,unmapCalls=0,releaseCalls=0;
void require(bool b){if(!b)throw std::runtime_error("GPU texture ownership contract failed");}
}
extern "C" uint64_t sceKernelGetDirectMemorySize(){return 16*1024*1024;}
extern "C" int sceKernelAllocateDirectMemory(int64_t,int64_t,size_t n,size_t align,int type,int64_t* out){
    ++allocCalls;*out=0x100000;require(!physical && align==65536 && type==12 && n%65536==0);
    if(failAllocate)return -1;
    physical=true;return 0;
}
extern "C" int sceKernelMapDirectMemory(void** out,size_t n,int prot,int,int64_t phys,size_t align){
    ++mapCalls;require(physical && !mapped && phys==0x100000 && prot==0x33 && align==65536);
    if(failMap){*out=reinterpret_cast<void*>(0xbad);return -1;}
    mapped=_aligned_malloc(n,align);require(mapped);*out=mapped;return 0;
}
extern "C" int sceKernelMunmap(void* p,size_t){
    ++unmapCalls;require(mapped && p==mapped);
    if(failUnmap)return -1;
    _aligned_free(mapped);mapped=nullptr;return 0;
}
extern "C" int sceKernelReleaseDirectMemory(int64_t phys,size_t){
    ++releaseCalls;require(physical && !mapped && phys==0x100000);
    if(failRelease)return -1;
    physical=false;return 0;
}
int main(){
    using mkw::agc::GpuTexture;
    try{
        std::vector<uint8_t> pixels(64*64*4,0x5a);
        for(int mode=0;mode<3;++mode){
            failAllocate=mode==1;failMap=mode==2;bool failed=false;
            auto oldUnmap=unmapCalls,oldRelease=releaseCalls;
            try{GpuTexture texture(64,64,1,pixels);require(texture.data()!=nullptr && physical && mapped);}
            catch(const std::exception&){failed=true;}
            require(failed==(mode!=0) && !physical && !mapped);
            if(mode==1)require(unmapCalls==oldUnmap && releaseCalls==oldRelease);
            if(mode==2)require(unmapCalls==oldUnmap && releaseCalls==oldRelease+1);
        }
        failAllocate=failMap=false;
        {
            GpuTexture texture(64,64,1,pixels);failUnmap=true;
            auto before=releaseCalls;
            require(texture.release_after_gpu_idle()!=0 && physical && mapped && releaseCalls==before);
            failUnmap=false;failRelease=true;
            require(texture.release_after_gpu_idle()!=0 && physical && !mapped);
            failRelease=false;
            require(texture.release_after_gpu_idle()==0 && !physical && !mapped);
            require(texture.release_after_gpu_idle()==0);
        }
        auto before=allocCalls;bool failed=false;
        try{GpuTexture texture(64,64,1,std::span(pixels).first(3));}catch(const std::exception&){failed=true;}
        require(failed && allocCalls==before);
        require(mapCalls>=3);
        std::puts("PASS GPU texture allocation/map rollback, failed unmap retention, release retry, idempotence and destructor cleanup");return 0;
    }catch(const std::exception& e){std::fprintf(stderr,"FAIL %s\n",e.what());return 1;}
}
