// Real descriptor/state/ownership code; only kernel allocation calls simulated.
#include "color_target.h"
#include <fstream>
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
        auto actual=encode_color_target(defaults,w,h,a);
        for(unsigned j=0;j<16;++j){if(actual[j]!=expected[j])std::fprintf(stderr,"case %u register %u: %08x != %08x\n",i,j,actual[j],expected[j]);check(actual[j]==expected[j],"SharpProspero target state");}
    }
    std::array<uint32_t,16> defaults{};
    rejects([&]{encode_color_target(defaults,0,64,0);});rejects([&]{encode_color_target(defaults,16385,64,0);});
    rejects([&]{encode_color_target(defaults,64,64,256);});rejects([&]{encode_color_target(defaults,64,64,1ull<<48);});
    failAlloc=true;rejects([]{GpuColorTarget a(64,64);});failAlloc=false;check(storage.empty(),"failed allocate leaves no storage");
    failMap=true;rejects([]{GpuColorTarget a(64,64);});failMap=false;check(storage.empty(),"map failure rolls back physical memory");
    {
        auto owner=std::make_shared<GpuColorTarget>(257,129);auto retained=owner;
        check(owner->allocation_size()==2097152,"scanout allocation rounds to 2 MiB");
        auto* data=static_cast<const uint32_t*>(owner->data());
        const size_t laidOut=(owner->layout().byte_size()+63)&~size_t(63);
        check(data[0]==0&&data[laidOut/4-1]==0,"new target layout initialized");
        owner->clear_after_gpu_idle(0x80402010);check(data[0]==0x80402010&&data[laidOut/4-1]==0x80402010,"idle clear covers the tiled layout");
        check(laidOut==owner->allocation_size()||data[laidOut/4]!=0x80402010,"idle clear leaves the unsampled allocation tail alone");
        auto texture=owner->texture_descriptor();check((texture[3]&0xfff)==(6u|(5u<<3)|(4u<<6)|(7u<<9)),"BGRA texture view");
        auto regs=owner->context_values(defaults);check(regs[0]==texture[0],"render and texture address agree");
        owner.reset();check(storage.size()==1,"retained draw owner keeps target alive");
        failUnmap=true;check(retained->release_after_gpu_idle()==-4&&retained->data()!=nullptr&&storage.size()==1,"unmap failure retains mapping and backing");failUnmap=false;
        failRelease=true;check(retained->release_after_gpu_idle()==-6&&retained->data()==nullptr&&storage.size()==1,"release failure retains physical handle for retry");failRelease=false;
        rejects([&]{retained->texture_descriptor();});rejects([&]{retained->context_values(defaults);});rejects([&]{retained->clear_after_gpu_idle(0);});
        check(retained->release_after_gpu_idle()==0&&storage.empty(),"release retry succeeds");
        check(retained->release_after_gpu_idle()==0,"repeated release is idempotent");
    }
    check(allocations==releases&&storage.empty(),"all allocations released");
    std::printf("PASS color target: %u checks, 64 SharpProspero blocks, ownership and injected allocation failures\n",checks);return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
