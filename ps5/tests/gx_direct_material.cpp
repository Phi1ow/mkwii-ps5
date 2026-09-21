// SPDX-License-Identifier: GPL-3.0-only
#include "gx_direct_material.h"
#include "gx_sampler.h"
#include "gx/register_state.hpp"
#include "gx/command_processor.hpp"
#include "gx/fifo.hpp"
#include "dolphin/gx/GXAurora.h"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <stdexcept>
#include <vector>
#include <limits>
using namespace mkw::agc;
namespace gx=aurora::gx;
static unsigned checks;
static void check(bool v,const char* label){++checks;if(!v)throw std::runtime_error(label);}
template<class F> static void rejects(F f){bool caught=false;try{f();}catch(const std::exception&){caught=true;}check(caught,"invalid material accepted");}
struct Sources {
    std::map<const void*,std::span<const uint8_t>> ranges;
    unsigned calls=0;
    static GxSourceBytes resolve(void* context,const GXTexObj_& t,const GXTlutObj_* p) {
        auto& s=*static_cast<Sources*>(context);++s.calls;
        return {s.ranges.at(t.data),p?s.ranges.at(p->data):std::span<const uint8_t>{}};
    }
};
static gx::GXRegisterState base() {
    gx::GXRegisterState s{};s.numTevStages=2;s.numTexGens=1;
    for(auto& c:s.colorRegs)c={0,0,0,0};for(auto& c:s.kcolors)c={0,0,0,0};
    for(unsigned i=0;i<2;++i) {
        auto& t=s.tevStages[i];t.texMapId=GXTexMapID(i?7:2);t.texCoordId=GX_TEXCOORD0;
        t.colorPass={GX_CC_ZERO,GX_CC_ZERO,GX_CC_ZERO,GX_CC_TEXC};
        t.alphaPass={GX_CA_ZERO,GX_CA_ZERO,GX_CA_ZERO,GX_CA_TEXA};
    }
    return s;
}
static GXTexObj_ init(std::vector<uint8_t>& bytes) {
    GXTexObj publicObject{};
    GXInitTexObj(&publicObject,bytes.data(),4,4,GX_TF_RGB565,GX_CLAMP,GX_REPEAT,false);
    return *reinterpret_cast<GXTexObj_*>(&publicObject);
}
static void descriptors(const char* file) {
    std::ifstream input(file,std::ios::binary);check(bool(input),"sampler reference open");
    std::array<uint32_t,8> row;unsigned count=0;
    while(input.read(reinterpret_cast<char*>(row.data()),sizeof row)) {
        GXTexObj_ t{};t.mode0=row[0];t.mode1=row[1];
        auto actual=gx_sampler(t,row[2],row[3]);
        check(std::equal(actual.begin(),actual.end(),row.begin()+4),"SharpProspero sampler descriptor mismatch");++count;
    }
    check(input.eof() && input.gcount()==0 && count==5760,"sampler reference count/truncation");
    GXTexObj_ t{};
    for(unsigned config:{0,3,17})rejects([&]{gx_sampler(t,false,config);});
    t.mode0=3;rejects([&]{gx_sampler(t,false,1);});
    t.mode0=1u<<5;t.mode1=32u|(16u<<8);rejects([&]{gx_sampler(t,false,1);});
    t.mode0=0;check(gx_sampler(t,false,1)[1]==0,"non-mip stale LOD range");
}
static void ownership() {
    GxTextureCache cache(0);Sources sources;
    std::vector<uint8_t> red(32),green(32);for(unsigned i=0;i<32;i+=2){red[i]=0xf8;green[i]=7;green[i+1]=0xe0;}
    auto s=base();s.loadedTextures[2]=init(red);s.loadedTextures[7]=init(green);
    sources.ranges={{red.data(),red},{green.data(),green}};
    s.texCoordScales[0].scaleS=3;s.texCoordScales[0].scaleT=7;s.texCoordScales[0].biasS=true;
    auto material=GxDirectMaterial::build(s,cache,Sources::resolve,&sources,4);
    check(material.texture_mask()==132 && sources.calls==2 && cache.cached_entries()==0,"only referenced maps resolved");
    const auto snapshot=material.uniforms();
    check(snapshot.coordinateScales[0]==std::array<float,4>{4,8,1,0},"GX coordinate scale snapshot");
    check(snapshot.textures[2].parameters==std::array<float,4>{1.f/512,1.f/512,0,0},"texture dimensions/bias uniforms");
    check(snapshot.textures[2].texture==material.textures()[2]->gpu.descriptor(),"GPU descriptor matches retained handle");
    check(snapshot.textures[0].texture==std::array<uint32_t,8>{} && !material.textures()[0],"unused slot zero");
    auto weak=std::weak_ptr(material.textures()[2]);
    s.tevStages[0].colorPass.d=GX_CC_ZERO;s.texCoordScales[0].scaleS=10;
    std::fill(red.begin(),red.end(),0);cache.clear();
    check(!std::memcmp(&snapshot,&material.uniforms(),sizeof snapshot),"GX mutation changes in-flight material");
    auto address=static_cast<const uint8_t*>(material.textures()[2]->gpu.data());
    check(address[0]==255 && address[1]==0 && address[2]==0,"uploaded pixels survive source mutation");
    auto retained=material;material={};check(!weak.expired(),"copied material retains textures");
    retained={};check(weak.expired(),"last retired material releases zero-budget texture");
    s=base();s.loadedTextures[2]=init(red);s.tevStages[1].texMapId=GX_TEXMAP2;sources.calls=0;
    material=GxDirectMaterial::build(s,cache,Sources::resolve,&sources,1);
    check(material.texture_mask()==4 && sources.calls==1,"duplicate texmap resolves once");
    s.numTexGens=0;sources.calls=0;
    material=GxDirectMaterial::build(s,cache,nullptr,nullptr,1);
    check(material.texture_mask()==0 && sources.calls==0,"disabled sampling reads no source");
    // Indirect stage 0 samples texmap 7 through coordinate 0 at 1/4 x 1/2 scale; stage 0 uses matrix 1.
    s=base();s.numIndStages=1;sources.calls=0;s.loadedTextures[2]=init(red);s.loadedTextures[7]=init(green);
    s.indStages[0]={GX_TEXCOORD0,GX_TEXMAP7,GX_ITS_4,GX_ITS_2};s.tevStages[0].indTexStage=GX_INDTEXSTAGE0;s.tevStages[0].indTexMtxId=GX_ITM_1;
    s.tevStages[1].texMapId=GX_TEXMAP_NULL;s.tevStages[1].colorPass.d=GX_CC_ZERO;s.tevStages[1].alphaPass.d=GX_CA_ZERO;
    s.indTexMtxs[1].mtx={{0.5f,-0.25f},{1.0f,0.0f},{0.125f,2.0f}};s.indTexMtxs[1].scaleExp=-3;
    material=GxDirectMaterial::build(s,cache,Sources::resolve,&sources,1);
    check(material.texture_mask()==132 && sources.calls==2,"indirect stage binds its own texture map");
    check(material.uniforms().indirectStages[0]==std::array<uint32_t,4>{7,0,2,1},"indirect stage binding uniform");
    check(material.uniforms().indirectMatrices[2]==std::array<int32_t,4>{512,-256,1024,0} && material.uniforms().indirectMatrices[3]==std::array<int32_t,4>{128,2048,3,0},"indirect matrix mantissas and shift");
    // Aurora tev_effective_texcoord: an index outside the generated set reads coordinate 0,
    // and without texgens the coordinate is the fixed zero the shader selects with 8.
    s.indStages[0].texCoordId=GX_TEXCOORD3;
    material=GxDirectMaterial::build(s,cache,Sources::resolve,&sources,1);
    check(material.uniforms().indirectStages[0]==std::array<uint32_t,4>{7,0,2,1},"indirect coordinate outside active texgens reads coordinate 0");
    s.numTexGens=0;s.tevStages[0].texMapId=GX_TEXMAP_NULL;
    material=GxDirectMaterial::build(s,cache,Sources::resolve,&sources,1);
    check(material.uniforms().indirectStages[0][1]==8 && material.texture_mask()==128,"indirect stage without texgens uses the fixed zero coordinate");
    s=base();s.numIndStages=1;s.indStages[0]={GX_TEXCOORD0,GX_TEXMAP_NULL,GX_ITS_4,GX_ITS_2};s.tevStages[0].indTexStage=GX_INDTEXSTAGE0;sources.calls=0;
    rejects([&]{GxDirectMaterial::build(s,cache,Sources::resolve,&sources,1);});
    check(sources.calls==0,"indirect stage without a texture map rejected before source reads");
    s=base();rejects([&]{GxDirectMaterial::build(s,cache,nullptr,nullptr,1);});
    s.loadedTextures[2]=init(red);s.loadedTextures[7]=init(green);sources.ranges[green.data()]={};
    const auto uploads=cache.uploads();
    rejects([&]{GxDirectMaterial::build(s,cache,Sources::resolve,&sources,1);});
    check(cache.uploads()==uploads+1 && cache.cached_entries()==0,"second upload failure rolls back material ownership");
}
static void palette_and_mips() {
    GxTextureCache cache;Sources sources;auto s=base();s.numTevStages=1;
    std::vector<uint8_t> indices(32,0),palette{0xf8,0};GXTexObj tex{};GXTlutObj tlut{};
    GXInitTexObjCI(&tex,indices.data(),8,8,GX_TF_C4,GX_CLAMP,GX_CLAMP,false,GX_TLUT2);
    GXInitTlutObj(&tlut,palette.data(),GX_TL_RGB565,1);
    s.loadedTextures[2]=*reinterpret_cast<GXTexObj_*>(&tex);
    s.loadedTluts[GX_TLUT2]=*reinterpret_cast<GXTlutObj_*>(&tlut);
    sources.ranges={{indices.data(),indices},{palette.data(),palette}};
    auto material=GxDirectMaterial::build(s,cache,Sources::resolve,&sources,1);
    check(static_cast<const uint8_t*>(material.textures()[2]->gpu.data())[0]==255,"loaded palette selected and converted");
    s.loadedTextures[2].tlut=GXTlut(10000);
    rejects([&]{GxDirectMaterial::build(s,cache,Sources::resolve,&sources,1);});
    std::vector<uint8_t> mips(224,0);for(unsigned i=0;i<128;i+=2)mips[i]=0xf8;
    GXInitTexObj(&tex,mips.data(),8,8,GX_TF_RGB565,GX_REPEAT,GX_REPEAT,true);
    GXInitTexObjLOD(&tex,GX_LIN_MIP_NEAR,GX_LINEAR,0,3,-0.5f,false,false,GX_ANISO_4);
    s.loadedTextures[2]=*reinterpret_cast<GXTexObj_*>(&tex);sources.ranges[mips.data()]=mips;
    s.logicalViewport.width=640;s.logicalViewport.height=480;s.renderViewport.width=1280;s.renderViewport.height=960;
    material=GxDirectMaterial::build(s,cache,Sources::resolve,&sources,4);
    check(material.textures()[2]->hasArbitraryMips,"authored mip detection required by test");
    check(material.uniforms().textures[2].parameters[2]==0.5f,"GX bias plus viewport bias");
    check(((material.uniforms().textures[2].sampler[2]>>26)&3)==2,"authored mips force linear mip filter");
    check(((material.uniforms().textures[2].sampler[0]>>9)&7)==2,"linear filters permit 4x anisotropy");
    auto noBias=GxDirectMaterial::build(s,cache,Sources::resolve,&sources,4,false);
    check(noBias.uniforms().textures[2].parameters[2]==-0.5f,"viewport option preserves GX bias");
    check(noBias.textures()[2]==material.textures()[2],"material changes reuse pixel storage");
    s.renderViewport.width=0;rejects([&]{GxDirectMaterial::build(s,cache,Sources::resolve,&sources,4);});
    s.renderViewport.width=std::numeric_limits<float>::infinity();
    rejects([&]{GxDirectMaterial::build(s,cache,Sources::resolve,&sources,4);});
}
static void gpu_copies() {
    auto& copies=gx_copy_texture_cache();copies.clear();GxTextureCache cache;Sources sources;
    auto s=base();s.numTevStages=1;s.tevStages[0].texMapId=GX_TEXMAP2;
    // A pointer token only: no material may read this stale guest destination.
    uint32_t destination=0;GXTexObj object{};
    GXInitTexObj(&object,&destination,64,32,GX_TF_RGBA8,GX_REPEAT,GX_CLAMP,false);
    s.loadedTextures[2]=*reinterpret_cast<GXTexObj_*>(&object);
    auto target=std::make_shared<GpuColorTarget>(192,96);
    target->clear_after_gpu_idle(0xff123456);
    copies.publish_rgba8(&destination,64,32,target);
    auto material=GxDirectMaterial::build(s,cache,Sources::resolve,&sources,1);
    check(sources.calls==0 && cache.uploads()==0,"GPU copy must bypass stale guest RAM and static upload");
    check(!material.textures()[2] && material.color_copies()[2]->target==target,"copy owner retained in material");
    check(material.uniforms().textures[2].texture==target->texture_descriptor(),"BGRA copy descriptor reaches TEV binding");
    check(material.uniforms().textures[2].parameters==std::array<float,4>{1.f/8192,1.f/4096,0,0},"upscaled copy retains logical GX coordinate domain");
    check(material.uniforms().textures[2].sampler==gx_sampler(s.loadedTextures[2],false,1),"copy preserves GX sampler");
    auto pureGpu=GxDirectMaterial::build(s,cache,nullptr,nullptr,1);
    check(pureGpu.color_copies()[2]==material.color_copies()[2],"GPU-only material needs no guest byte resolver");
    auto before=material.uniforms();auto weak=std::weak_ptr<const GpuColorTarget>(target);
    auto replacement=std::make_shared<GpuColorTarget>(128,64);
    copies.publish_rgba8(&destination,64,32,replacement);target.reset();
    check(!weak.expired() && !std::memcmp(&before,&material.uniforms(),sizeof before),"publishing next copy preserves previously recorded material");
    auto next=GxDirectMaterial::build(s,cache,nullptr,nullptr,1);
    check(next.color_copies()[2]->target==replacement,"next material resolves replacement target");
    rejects([&]{copies.publish_rgba8(&destination,0,32,replacement);});
    rejects([&]{copies.publish_rgba8(&destination,64,32,{});});
    rejects([&]{copies.publish_rgba8(nullptr,64,32,replacement);});
    check(copies.resolve(s.loadedTextures[2])==next.color_copies()[2],"failed publication preserves previous valid copy");
    // Explicitly reject format/size/mip reinterpretations until converted copies
    // and deferred guest readback exist, never silently upload stale bytes.
    for(auto format:{GX_TF_RGB565,static_cast<GXTexFmt>(GX_TF_C4)}) {
        GXInitTexObj(&object,&destination,64,32,format,GX_CLAMP,GX_CLAMP,false);
        auto incompatible=*reinterpret_cast<GXTexObj_*>(&object);
        rejects([&]{copies.resolve(incompatible);});
    }
    GXInitTexObj(&object,&destination,32,32,GX_TF_RGBA8,GX_CLAMP,GX_CLAMP,false);
    rejects([&]{copies.resolve(*reinterpret_cast<GXTexObj_*>(&object));});
    GXInitTexObj(&object,&destination,64,32,GX_TF_RGBA8,GX_CLAMP,GX_CLAMP,true);
    GXInitTexObjLOD(&object,GX_LIN_MIP_LIN,GX_LINEAR,0,5,0,false,false,GX_ANISO_1);
    rejects([&]{copies.resolve(*reinterpret_cast<GXTexObj_*>(&object));});
    // The actual producer + FIFO dispatch removes the published destination.
    GXDestroyCopyTex(&destination);gx::fifo::drain();
    check(copies.size()==0 && !copies.resolve(s.loadedTextures[2]),"GXDestroyCopyTex reaches production copy cache");
    check(!weak.expired() && next.color_copies()[2]->target==replacement,"eviction preserves both retained revisions");
    material={};pureGpu={};check(weak.expired(),"retired materials release superseded target");
    // A mixed material resolves just the static map's guest bytes.
    std::vector<uint8_t> green(32);for(unsigned i=0;i<32;i+=2){green[i]=7;green[i+1]=0xe0;}
    copies.publish_rgba8(&destination,64,32,replacement);s.numTevStages=2;s.loadedTextures[7]=init(green);
    sources.ranges[green.data()]=green;sources.calls=0;
    auto mixed=GxDirectMaterial::build(s,cache,Sources::resolve,&sources,1);
    check(sources.calls==1 && mixed.color_copies()[2] && mixed.textures()[7],"mixed copy/static material resolves independent ownership");
    check(cache.uploads()==1,"only static member uploads pixels");
    copies.clear();cache.clear();
    check(mixed.color_copies()[2]->target->data()!=nullptr,"cache clear cannot release in-flight target");
    // Independently scoped cache support for renderer instances.
    GxCopyTextureCache local;local.publish_rgba8(&destination,64,32,replacement);
    s.numTevStages=1;
    auto isolated=GxDirectMaterial::build(s,cache,nullptr,nullptr,1,true,&local);
    check(isolated.color_copies()[2]->target==replacement && copies.size()==0,"explicit renderer cache selected");
}
static void copy_formats() {
    struct Format{GXTexFmt produced;unsigned sampled;};
    const Format formats[]={{GX_TF_I4,0},{GX_TF_I8,1},{GX_TF_IA4,2},{GX_TF_IA8,3},{GX_TF_RGB565,4},
        {GX_TF_RGB5A3,5},{GX_TF_RGBA8,6},{GX_CTF_R4,0},{GX_CTF_RA4,2},{GX_CTF_RA8,3},
        {GX_CTF_A8,1},{GX_CTF_R8,1},{GX_CTF_G8,1},{GX_CTF_B8,1},{GX_CTF_RG8,3},{GX_CTF_GB8,3},
        {GX_TF_Z16,GX_TF_IA8},{GX_TF_Z24X8,GX_TF_RGBA8}};
    GxCopyTextureCache copies;GxTextureCache textures;auto target=std::make_shared<GpuColorTarget>(8,8);
    auto state=base();state.numTevStages=1;state.tevStages[0].texMapId=GX_TEXMAP2;uint32_t token=0;
    for(auto f:formats){
        copies.publish(&token,8,8,f.produced,target);
        for(unsigned sampled=0;sampled<48;++sampled){
            GXTexObj_ object;object.data=&token;object.mWidth=object.mHeight=8;object.mFormat=sampled;
            bool compatible=sampled==unsigned(f.produced)||sampled==f.sampled;
            if(f.produced==GX_CTF_A8||f.produced==GX_CTF_R8||f.produced==GX_CTF_G8||f.produced==GX_CTF_B8)
                compatible=compatible||sampled==GX_TF_A8;
            if(compatible){auto copy=copies.resolve(object);check(copy&&copy->format==f.produced,"copy keeps produced format across sampling alias");}
            else rejects([&]{copies.resolve(object);});
        }
        GXTexObj object{};GXInitTexObj(&object,&token,8,8,GXTexFmt(f.sampled),GX_CLAMP,GX_CLAMP,false);
        state.loadedTextures[2]=*reinterpret_cast<GXTexObj_*>(&object);
        auto material=GxDirectMaterial::build(state,textures,nullptr,nullptr,1,true,&copies);
        check(material.color_copies()[2]->format==f.produced&&material.color_copies()[2]->target==target,"material resolves typed copy without guest byte reader");
        check(textures.uploads()==0,"typed copy never uploads stale RAM");
    }
    auto previous=copies.resolve(state.loadedTextures[2]);
    // Z16/Z24X8 depth copies publish and resolve through their IA8/RGBA8
    // sampling aliases; Z8, CTF depth sub-formats, palettes and YUV stay out.
    for(auto unsupported:{GX_TF_Z8,GX_CTF_Z16L,static_cast<GXTexFmt>(GX_TF_C4),GX_CTF_YUVA8})rejects([&]{copies.publish(&token,8,8,unsupported,target);});
    check(copies.resolve(state.loadedTextures[2])==previous,"unsupported publication preserves last typed copy");
}
void test_gx_materials(const char* fixture) {
    descriptors(fixture);ownership();palette_and_mips();gpu_copies();copy_formats();
    extern void test_gx_copy_readbacks();test_gx_copy_readbacks();
    std::printf("PASS GX direct materials: %u checks, 5760 SharpProspero sampler comparisons; simulated GPU memory, no rendering\n",checks);
}
