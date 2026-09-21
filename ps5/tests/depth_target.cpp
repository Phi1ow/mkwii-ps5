// Real descriptor/state/ownership code; only kernel allocation calls simulated.
#include "depth_target.h"
#include <fstream>
#include <vector>
#include <bit>
#include <limits>
#include <map>
#include <memory>
#include <malloc.h>
#include <cstdio>
#include <stdexcept>
using namespace mkw::agc;
static unsigned checks,allocations,releases;
static bool failAlloc,failMap,failUnmap,failRelease;
struct Storage {size_t size;void* mapped=nullptr;};
static std::map<int64_t,Storage> storage;
static int64_t nextPhysical=0;
static void check(bool v,const char* m){++checks;if(!v)throw std::runtime_error(m);}
extern "C" uint64_t sceKernelGetDirectMemorySize(){return 1ull<<32;}
extern "C" int sceKernelAllocateDirectMemory(int64_t,int64_t,size_t size,size_t alignment,int type,int64_t* out){
    check(alignment==2097152&&size%alignment==0&&type==12,"allocation contract");
    if(failAlloc)return -1;*out=nextPhysical;nextPhysical+=size;storage.emplace(*out,Storage{size});++allocations;return 0;
}
extern "C" int sceKernelMapDirectMemory(void** out,size_t size,int protection,int flags,int64_t offset,size_t alignment){
    check(storage.contains(offset)&&storage.at(offset).size==size&&protection==0x33&&flags==0&&alignment==2097152,"mapping contract");
    if(failMap)return -2;auto p=_aligned_malloc(size,alignment);if(!p)return -3;
    *out=p;storage.at(offset).mapped=p;return 0;
}
extern "C" int sceKernelMunmap(void* p,size_t size){
    for(auto& [offset,s]:storage)if(s.mapped==p){check(s.size==size,"unmap size");if(failUnmap)return -4;_aligned_free(p);s.mapped=nullptr;return 0;}
    return -5;
}
extern "C" int sceKernelReleaseDirectMemory(int64_t offset,size_t size){
    check(storage.contains(offset)&&storage.at(offset).size==size&&!storage.at(offset).mapped,"release after unmap");
    if(failRelease)return -6;storage.erase(offset);++releases;return 0;
}
template<class F>static void rejects(F&& f){bool rejected=false;try{f();}catch(const std::exception&){rejected=true;}check(rejected,"invalid input rejected");}
int main(int argc,char** argv){try{
    if(argc!=2)return 2;std::ifstream in(argv[1],std::ios::binary);uint32_t count;in.read(reinterpret_cast<char*>(&count),4);check(count==64&&in.good(),"reference count");
    for(unsigned i=0;i<count;++i){uint32_t w,h;uint64_t a;std::array<uint32_t,16> defaults,expected;
        in.read(reinterpret_cast<char*>(&w),4);in.read(reinterpret_cast<char*>(&h),4);in.read(reinterpret_cast<char*>(&a),8);
        in.read(reinterpret_cast<char*>(defaults.data()),64);in.read(reinterpret_cast<char*>(expected.data()),64);check(in.good(),"reference bytes");
        auto actual=encode_depth_target(defaults,w,h,a);
        for(unsigned j=0;j<16;++j){if(actual[j]!=expected[j])std::fprintf(stderr,"case %u register %u: %08x != %08x\n",i,j,actual[j],expected[j]);check(actual[j]==expected[j],"SharpProspero target state");}
    }
    uint32_t surfaces;in.read(reinterpret_cast<char*>(&surfaces),4);check(surfaces==46,"surface count");
    for(unsigned n=0;n<surfaces;++n){uint32_t w,h;uint64_t size;
        in.read(reinterpret_cast<char*>(&w),4);in.read(reinterpret_cast<char*>(&h),4);in.read(reinterpret_cast<char*>(&size),8);
        check(in.good()&&size<=64*1024*1024,"valid surface record");
        std::vector<uint32_t> tiled(size/4);in.read(reinterpret_cast<char*>(tiled.data()),size);check(in.good(),"complete tiled reference");
        DepthLayout layout(w,h);check(layout.byte_size()==size,"SharpProspero depth size");
        for(uint32_t y=0;y<h;++y)for(uint32_t x=0;x<w;++x){auto offset=layout.pixel_offset(x,y);
            check(offset%4==0&&offset<size,"depth address bounds");check(tiled[offset/4]==y*w+x+1,"SharpProspero tiled depth pixel");}
        rejects([&]{layout.pixel_offset(w,0);});rejects([&]{layout.pixel_offset(0,h);});
    }
    rejects([]{DepthLayout bad(0,1);});rejects([]{DepthLayout bad(1,16385);});
    std::array<uint32_t,16> defaults{};
    rejects([&]{encode_depth_target(defaults,0,64,0);});rejects([&]{encode_depth_target(defaults,16385,64,0);});
    rejects([&]{encode_depth_target(defaults,64,64,256);});rejects([&]{encode_depth_target(defaults,64,64,1ull<<48);});
    failAlloc=true;rejects([]{GpuDepthTarget a(64,64);});failAlloc=false;check(storage.empty(),"failed allocate leaves no storage");
    failMap=true;rejects([]{GpuDepthTarget a(64,64);});failMap=false;check(storage.empty(),"map failure rolls back physical memory");
    {
        auto owner=std::make_shared<GpuDepthTarget>(257,129);auto retained=owner;
        check(owner->allocation_size()==2097152,"scanout allocation rounds to 2 MiB");
        auto* data=static_cast<const uint32_t*>(owner->data());
        check(data[0]==0x3f800000&&data[owner->allocation_size()/4-1]==0x3f800000,"new target and padding initialized");
        owner->clear_after_gpu_idle(.25f);check(data[0]==0x3e800000&&data[owner->allocation_size()/4-1]==0x3e800000,"idle clear covers allocation");
        auto regs=owner->context_values(defaults);check(regs[2]==uint32_t(reinterpret_cast<uintptr_t>(owner->data())>>8),"depth address agrees");
        rejects([&]{owner->clear_after_gpu_idle(-1);});rejects([&]{owner->clear_after_gpu_idle(2);});
        rejects([&]{owner->clear_after_gpu_idle(std::numeric_limits<float>::quiet_NaN());});
        owner.reset();check(storage.size()==1,"retained draw owner keeps target alive");
        failUnmap=true;check(retained->release_after_gpu_idle()==-4&&retained->data()!=nullptr&&storage.size()==1,"unmap failure retains mapping and backing");failUnmap=false;
        failRelease=true;check(retained->release_after_gpu_idle()==-6&&retained->data()==nullptr&&storage.size()==1,"release failure retains physical handle for retry");failRelease=false;
        rejects([&]{retained->context_values(defaults);});rejects([&]{retained->clear_after_gpu_idle(0);});
        check(retained->release_after_gpu_idle()==0&&storage.empty(),"release retry succeeds");
        check(retained->release_after_gpu_idle()==0,"repeated release is idempotent");
    }
    check(allocations==releases&&storage.empty(),"all allocations released");
    std::printf("PASS depth target: %u checks, 64 SharpProspero blocks, 46 tiled surfaces, ownership and injected allocation failures\n",checks);return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
