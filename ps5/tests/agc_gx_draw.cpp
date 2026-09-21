// SPDX-License-Identifier: GPL-3.0-only
// Real GX producers, FIFO decoder, Aurora pixel conversion and AGC tiling.
// Only the physical-memory API is simulated; no GPU rendering is claimed.
#include "agc_gx_draw.h"
#include "gx_renderer.h"
#include "gx_draw_fixture.h"
#include "dolphin/gx/frontend.hpp"
#include "dolphin/gx/__gx.h"
#include "gx/command_processor.hpp"
#include "gx/register_backend.hpp"
#include <aurora/gfx.h>
#include <dolphin/gx/GXAurora.h>
#include <malloc.h>
#include <map>
#include <cstdio>
#include <stdexcept>
#include <exception>
#include <vector>

namespace gx=aurora::gx;
namespace fifo=aurora::gx::fifo;
using mkw::agc::GxTextureCache;
using mkw::agc::TextureHandle;
static __GXData_struct shadow{};
__GXData_struct* __gx=&shadow;
static unsigned checks,allocations,releases;
static bool failMap;
struct Allocation { size_t bytes,alignment; void* address=nullptr; };
static std::map<int64_t,Allocation> physical;
static int64_t nextPhysical=0x100000;
static void require(bool value,const char* label) { ++checks; if(!value) throw std::runtime_error(label); }
extern "C" uint64_t sceKernelGetDirectMemorySize() { return uint64_t{1}<<32; }
extern "C" int sceKernelAllocateDirectMemory(int64_t,int64_t,size_t bytes,size_t align,int type,int64_t* out) {
    require((align==65536||align==2*1024*1024) && type==12 && bytes%align==0,"GPU allocation contract");
    *out=nextPhysical; nextPhysical+=static_cast<int64_t>(bytes);
    physical.emplace(*out,Allocation{bytes,align}); ++allocations; return 0;
}
extern "C" int sceKernelMapDirectMemory(void** out,size_t bytes,int prot,int,int64_t offset,size_t align) {
    require(physical.contains(offset) && physical.at(offset).bytes==bytes && prot==0x33 && align==physical.at(offset).alignment,"GPU mapping contract");
    if(failMap) return -1;
    *out=_aligned_malloc(bytes,align); if(!*out) return -1;
    physical.at(offset).address=*out; return 0;
}
extern "C" int sceKernelMunmap(void* address,size_t bytes) {
    for(auto& [offset,allocation]:physical) if(allocation.address==address) {
        require(allocation.bytes==bytes,"GPU unmap size"); _aligned_free(address); allocation.address=nullptr; return 0;
    }
    throw std::runtime_error("unknown GPU mapping");
}
extern "C" int sceKernelReleaseDirectMemory(int64_t offset,size_t bytes) {
    require(physical.contains(offset) && physical.at(offset).bytes==bytes && !physical.at(offset).address,"GPU release after unmap");
    physical.erase(offset); ++releases; return 0;
}
namespace aurora {
AuroraConfig g_config{};
std::chrono::nanoseconds wait_for_frame_worker_sealed() noexcept { return {}; }
}
// Unused renderer callbacks fail this test if accidentally reached.
namespace aurora::gx {
void set_logical_viewport(const gfx::Viewport&) noexcept { std::terminate(); }
void set_logical_scissor(const gfx::ClipRect&) noexcept { std::terminate(); }
void set_render_viewport(const gfx::Viewport&) noexcept { std::terminate(); }
void set_render_scissor(const gfx::ClipRect&) noexcept { std::terminate(); }

}
namespace aurora::gfx {
void push_debug_group(std::string) { throw std::runtime_error("unexpected debug group"); }
void insert_debug_marker(std::string) { throw std::runtime_error("unexpected debug marker"); }
}
extern "C" void __GXSetDirtyState(){throw std::runtime_error("Unexpected dirty flush");}
extern "C" void aurora_pop_debug_group() { std::terminate(); }


using namespace mkw::agc;
static unsigned submissions,polls,lastBatchDraws;static bool timeout,failSubmit;
static std::map<unsigned,const uint8_t*> descriptorAddress; // last address bound per SH offset
struct Register{uint16_t offset,pad;uint32_t value;};
struct Dcb{uint32_t *bottom,*top,*up,*down;intptr_t callback;void* user;uint32_t reserved,pad;};
static std::array<Register,34> context;
static std::array<unsigned char,64> defaults;
static std::map<uint16_t,uint32_t> observedContext;
static std::map<unsigned,std::vector<uint8_t>> uploaded;
extern "C" void* sceAgcGetRegisterDefaults(){return defaults.data();}
extern "C" int sceAgcLinkShaders(void* linkage,void* primitive,void*,void*,void*,unsigned type){
 require(type==4,"triangle primitive");auto* r=static_cast<Register*>(linkage);for(unsigned i=0;i<34;++i)r[i]={uint16_t(0x191+i),0,i};std::memset(primitive,0,24);return 0;}
extern "C" void sceAgcDcbSetCxRegistersIndirect(void*,void* p,unsigned count){
 observedContext.clear();auto* r=static_cast<Register*>(p);for(unsigned i=0;i<count;++i)observedContext[r[i].offset]=r[i].value;}
extern "C" void sceAgcDcbSetShRegistersIndirect(void*,void*,unsigned n){require(!n,"empty mocked shader registers");}
extern "C" void sceAgcDcbSetUcRegistersIndirect(void*,void*,unsigned n){require(n==3,"primitive record count");}
extern "C" void sceAgcCbSetShRegisterRangeDirect(void*,unsigned offset,unsigned* words,unsigned n){
 require(n==4&&words[3]==0x30005204,"raw buffer descriptor");auto* p=reinterpret_cast<const uint8_t*>(uint64_t(words[0])|(uint64_t(words[1]&65535)<<32));
 uploaded[offset]={p,p+words[2]};descriptorAddress[offset]=p;}
extern "C" void* sceAgcDcbSetIndexSize(void*,unsigned char size,unsigned char cache){require(size==1&&!cache,"index size");return nullptr;}
extern "C" void* sceAgcDcbSetIndexBuffer(void*,void* p){require(p!=nullptr,"index pointer");return nullptr;}
extern "C" void* sceAgcDcbSetIndexCount(void*,unsigned n){require(n==6,"quad indices");return nullptr;}
extern "C" void* sceAgcDcbDrawIndex(void* p,unsigned n,void* indices,uint64_t modifier){
 const uint32_t expected[6]={0,1,2,2,3,0};require(n==6&&!modifier&&!std::memcmp(indices,expected,sizeof expected),"immutable generated indices");
 *static_cast<Dcb*>(p)->up++=0x1234;return nullptr;}
extern "C" int sceAgcDriverSubmitDcb(void* p){
 struct Submit{uint32_t* words;uint32_t count;uint8_t flag;};auto* s=static_cast<Submit*>(p);
 require(s->count>=9,"batch holds at least one draw and the fence");
 const uint32_t draws=s->count-8;for(uint32_t i=0;i<draws;++i)require(s->words[i]==0x1234,"draw packets precede the fence");
 require(s->words[draws]==0xc0064900,"completion packet ends the batch");++submissions;lastBatchDraws=draws;if(failSubmit)return -1;
 auto* v=s->words+draws;if(!timeout)*reinterpret_cast<uint64_t*>(uint64_t(v[3])|(uint64_t(v[4])<<32))=uint64_t(v[5])|(uint64_t(v[6])<<32);return 0;}
extern "C" int sceAgcSuspendPoint(){return 0;}
extern "C" int sceKernelUsleep(unsigned){++polls;return 0;}
template<class T>void put(void* p,size_t n,T v){std::memcpy(static_cast<char*>(p)+n,&v,sizeof v);}
template<class F>void rejects(F f){bool rejected=false;try{f();}catch(const std::exception&){rejected=true;}require(rejected,"expected rejection");}
int main(){try{
 for(unsigned i=0;i<16;++i){context[i]={ColorTargetOffsets[i],0,0};context[16+i]={DepthTargetOffsets[i],0,0};}
 context[32]={0x205,0,0};context[33]={0x1e0,0,0};Register* records=context.data();put(defaults.data(),0,&records);put(defaults.data(),0x20,uint32_t(context.size()));
 std::array<unsigned char,128> vs{},ps{},vu{},pu{};uint16_t vertex=4,constant=0,texture=0x8000;
 put(vs.data(),8,vu.data());put(ps.data(),8,pu.data());put(vu.data(),8,&vertex);put(vu.data(),32,&constant);put(vu.data(),46,uint16_t(1));put(vu.data(),52,uint16_t(1));
 put(pu.data(),8,&texture);put(pu.data(),46,uint16_t(1));
 auto state=mkw::test::draw_state(64,32);GxTextureCache cache;
 const unsigned reverse[8]={0,4,2,6,1,5,3,7};
 for(unsigned compare=0;compare<8;++compare)for(bool test:{false,true})for(bool write:{false,true}){
  auto s=state;s.depthFunc=GXCompare(compare);s.depthCompare=test;s.depthUpdate=write;
  require(snapshot_gx_depth_control(s)==(2u|(write?4u:0u)|((test?reverse[compare]:7u)<<4)),"GX reversed depth state");}
 auto geometry=mkw::test::draw_geometry(state);auto packet=GxDrawPacket::build(geometry,state,cache,nullptr,nullptr);
 {auto unsupported=state;unsupported.fog.type=GX_FOG_PERSP_LIN;
  auto fogged=GxDrawPacket::build(geometry,unsupported,cache,nullptr,nullptr);
  require(fogged.material.uniforms().fog.ctl[0]==unsigned(GX_FOG_PERSP_LIN)&&!fogged.material.uniforms().fog.ctl[1],
   "fog draw builds the extended fragment uniform");
  unsupported.fog.type=GXFogType(8); // Outside the reference shader switch.
  rejects([&]{GxDrawPacket::build(geometry,unsupported,cache,nullptr,nullptr);});
  unsupported=state;unsupported.zTextureOp=GX_ZT_REPLACE;
  rejects([&]{GxDrawPacket::build(geometry,unsupported,cache,nullptr,nullptr);});
  require(submissions==0,"unsupported shader states cannot submit");
  // Mario Kart Wii enables GX_ZCOMPLOC for its first frame; it must build like the reference pipeline.
  auto earlyDepth=state;earlyDepth.zCompLocBeforeTex=true;auto early=GxDrawPacket::build(geometry,earlyDepth,cache,nullptr,nullptr);
  require(early.depthControl==packet.depthControl&&early.geometry==packet.geometry,"early depth policy accepted without changing the draw");}
 auto raw=packet.geometry;auto transforms=packet.transforms;auto material=packet.material.uniforms();
 state.proj={};state.colorRegs[0]={0,1,0,1};state.renderViewport={};
 auto color=std::make_shared<GpuColorTarget>(64,32);auto depth=std::make_shared<GpuDepthTarget>(64,32);
 {AgcGxDraw renderer(vs.data(),ps.data());auto before=physical.size();
  rejects([&]{renderer.draw(packet,color,nullptr);});require(submissions==0&&physical.size()==before,"invalid target changes nothing");
  require(renderer.draw(packet,color,depth)==1&&submissions==0&&!renderer.pending(),"draw is recorded, not submitted");
  require(uploaded.at(0x90)==raw,"raw vertices preserved without CPU decoding");
  require(!std::memcmp(uploaded.at(0x8c).data(),&transforms,sizeof transforms),"matrix snapshot uploaded");
  require(!std::memcmp(uploaded.at(0xc).data(),&material,sizeof material),"TEV snapshot uploaded");
  require(observedContext.at(0x200)==0x46&&observedContext.at(0x8e)==15,"GX depth and color state");
  renderer.finish();require(submissions==1&&lastBatchDraws==1&&!renderer.pending(),"finish submits the open batch");
  // Two identical draws share one batch: one submission, fresh geometry, shared transform/material uploads.
  auto allocated=allocations;
  require(renderer.draw(packet,color,depth)==2,"second serial");
  const auto* vertexState=descriptorAddress.at(0x8c);const auto* materialState=descriptorAddress.at(0xc);const auto* firstGeometry=descriptorAddress.at(0x90);
  require(renderer.draw(packet,color,depth)==3,"third serial");
  require(descriptorAddress.at(0x8c)==vertexState&&descriptorAddress.at(0xc)==materialState,"identical vertex state and material uploaded once per batch");
  require(descriptorAddress.at(0x90)!=firstGeometry,"each draw keeps its own geometry");
  require(submissions==1,"batched draws wait for finish");
  auto empty=packet;empty.viewport.scissor.width=0;require(renderer.draw(empty,color,depth)==3,"empty draw takes no serial");
  empty=packet;empty.cullMode=3;require(renderer.draw(empty,color,depth)==3,"cull all takes no serial");
  renderer.finish();require(submissions==2&&lastBatchDraws==2,"one submission carries both recorded draws");
  require(allocations==allocated+2,"a new batch allocates its command and upload buffers once");
  auto sampledState=mkw::test::draw_state(64,32);sampledState.numTexGens=1;
  sampledState.tcgs[0].type=GX_TG_MTX2x4;sampledState.tcgs[0].src=GX_TG_POS;sampledState.tcgs[0].mtx=GX_IDENTITY;sampledState.tcgs[0].postMtx=GX_PTIDENTITY;
  sampledState.tevStages[0].texMapId=GX_TEXMAP0;sampledState.tevStages[0].texCoordId=GX_TEXCOORD0;
  sampledState.tevStages[0].colorPass.d=GX_CC_TEXC;sampledState.tevStages[0].alphaPass.d=GX_CA_TEXA;
  uint32_t token=0;GXTexObj tex{};GXInitTexObj(&tex,&token,64,32,GX_TF_RGBA8,GX_CLAMP,GX_CLAMP,false);
  sampledState.loadedTextures[0]=*reinterpret_cast<GXTexObj_*>(&tex);
  auto& copies=gx_copy_texture_cache();copies.publish_rgba8(&token,64,32,color);
  auto feedback=GxDrawPacket::build(geometry,sampledState,cache,nullptr,nullptr);
  rejects([&]{renderer.draw(feedback,color,depth);});renderer.finish();require(submissions==2,"render feedback rejected before GPU work");
  auto source=std::make_shared<GpuColorTarget>(64,32);std::weak_ptr<GpuColorTarget> weak=source;
  copies.publish_rgba8(&token,64,32,source);auto sampled=GxDrawPacket::build(geometry,sampledState,cache,nullptr,nullptr);
  source.reset();copies.clear();require(!weak.expired(),"draw packet retains evicted EFB texture");
  require(renderer.draw(std::move(sampled),color,depth)==4&&!weak.expired(),"open batch retains the sampled EFB texture");
  renderer.finish();require(weak.expired(),"retired batch releases the last sampled EFB owner");
  // A full batch submits by itself; the remainder waits for finish.
  auto submittedBefore=submissions;uint64_t last=4;
  for(unsigned i=0;i<AgcGxDraw::BatchDraws+1;++i)last=renderer.draw(packet,color,depth);
  require(last==4+AgcGxDraw::BatchDraws+1&&submissions==submittedBefore+1&&lastBatchDraws==AgcGxDraw::BatchDraws,"full batch submitted automatically");
  renderer.finish();require(submissions==submittedBefore+2&&lastBatchDraws==1,"remainder submitted on finish");
  // Batches rotate through the in-flight ring; once every slot exists no further GPU memory is allocated.
  for(unsigned i=0;i<AgcGxDraw::InFlightSlots;++i){renderer.draw(packet,color,depth);renderer.finish();}
  auto filled=allocations;
  for(unsigned i=0;i<2*AgcGxDraw::InFlightSlots;++i){renderer.draw(packet,color,depth);renderer.finish();}
  require(allocations==filled&&!renderer.pending(),"in-flight batches reuse their storage");
 }
 {
  auto s=mkw::test::draw_state(64,32);auto g=mkw::test::draw_geometry(s);auto positions=g.records();
  s.vtxDesc[GX_VA_POS]=GX_INDEX8;s.arrays[GX_VA_POS]={positions.data(),uint32_t(positions.size()),12,false,{}};
  gx::register_state()=s;const uint8_t command[]={0x80,0,4,0,1,2,3};auto before=submissions;
  rejects([&]{gx::fifo::process(command,sizeof command,true);});require(submissions==before,"missing receiver fails before GPU work");
  auto array=+[](void* p,GXAttr a,const gx::AttrArray& arr,uint32_t off,uint32_t bytes)->std::span<const uint8_t>{
   auto& v=*static_cast<std::vector<uint8_t>*>(p);require(a==GX_VA_POS&&arr.data==v.data()&&off<=v.size()&&bytes<=v.size()-off,"registered vertex source range");return std::span(v).subspan(off,bytes);};
  auto source=+[](void*,const GXTexObj_&,const GXTlutObj_*)->GxSourceBytes{throw std::logic_error("unexpected texture source");};
  GxRenderer renderer(vs.data(),ps.data(),color,depth,array,source,&positions);
  {ScopedGxDrawSink scope(renderer.sink());gx::fifo::process(command,sizeof command,true);renderer.finish();
   require(renderer.completed_draws()==1&&submissions==before+1,"actual FIFO reaches GPU renderer");}
  rejects([&]{gx::fifo::process(command,sizeof command,true);});require(submissions==before+1,"scoped receiver restored");
 }
 color.reset();depth.reset();require(physical.empty(),"idle draw resources released");
 for(bool submitError:{false,true}){
  color=std::make_shared<GpuColorTarget>(64,32);depth=std::make_shared<GpuDepthTarget>(64,32);
  std::weak_ptr<GpuColorTarget> wc=color;std::weak_ptr<GpuDepthTarget> wz=depth;timeout=!submitError;failSubmit=submitError;auto before=submissions;
  {AgcGxDraw renderer(vs.data(),ps.data());
   // Recording never fails; the submission error or a GPU that never reports completion surfaces from finish().
   require(renderer.draw(packet,color,depth)==1&&!renderer.pending(),"recorded draw is not pending");
   rejects([&]{renderer.finish();});
   require(renderer.pending(),"failed draw stays pending");
   rejects([&]{renderer.draw(packet,color,depth);});rejects([&]{renderer.finish();});require(submissions==before+1,"unresolved draw not submitted twice");}
  color.reset();depth.reset();require(!wc.expired()&&!wz.expired()&&physical.size()==5,"targets, completion label and the batch's two GPU buffers retained");
  for(auto& [id,a]:physical){(void)id;_aligned_free(a.address);}physical.clear();
 }
 require(polls==5000,"bounded timeout");std::printf("PASS %u GX draw snapshot, batching, deduplication, allocation reuse and timeout checks; simulated driver only\n",checks);return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL %s\n",e.what());return 1;}}
