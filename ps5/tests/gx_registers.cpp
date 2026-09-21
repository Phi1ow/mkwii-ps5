// Uses the production FIFO dispatcher, register decoder and native register
// storage. Only renderer callbacks are observers: no draw is rendered here.
#include "dolphin/gx/frontend.hpp"
#include "dolphin/gx/__gx.h"
#include "gx/command_processor.hpp"
#include "gx/register_backend.hpp"
#include "gx/register_decoder.hpp"
#include <dolphin/gx/GXAurora.h>
#include <aurora/gfx.h>
#include <bit>
#include <cstdio>
#include <cstdlib>
#include <span>
#include <stdexcept>
#include <vector>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace gx = aurora::gx;
namespace fifo = aurora::gx::fifo;
static __GXData_struct shadow{};
__GXData_struct* __gx = &shadow;
static unsigned checks, draws, viewportUpdates, scissorUpdates, invalidates, pops, waits;
static u32 evictedTexture, evictedTlut;
static const void* evictedCopy;
static std::string debugGroup, debugMarker;
static std::vector<u8> drawBytes;
static bool drawBigEndian;
namespace aurora {
AuroraConfig g_config{};
std::chrono::nanoseconds wait_for_frame_worker_sealed() noexcept { ++waits; return {}; }
}
namespace aurora::gx {
void set_logical_viewport(const gfx::Viewport& v) noexcept { ++viewportUpdates; register_state().logicalViewport = v; }
void set_logical_scissor(const gfx::ClipRect& v) noexcept { ++scissorUpdates; register_state().logicalScissor = v; }
void set_render_viewport(const gfx::Viewport& v) noexcept { register_state().renderViewport = v; }
void set_render_scissor(const gfx::ClipRect& v) noexcept { register_state().renderScissor = v; }
void evict_texture_object(u32 id) noexcept { evictedTexture = id; }
void evict_tlut_object(u32 id) noexcept { evictedTlut = id; }
void evict_copy_texture(const void* p) noexcept { evictedCopy = p; }
void invalidate_static_texture_cache() noexcept { ++invalidates; }
namespace fifo {
bool handle_draw(u8 command, const u8* data, u32& pos, u32 size, bool bigEndian) {
  ++draws; drawBigEndian = bigEndian; drawBytes = {command};
  drawBytes.insert(drawBytes.end(), data + pos, data + size);
  return false; // Stop after observing the packet. No fake GPU submission.
}
}
}
namespace aurora::gfx {
void push_debug_group(std::string v) { debugGroup = std::move(v); }
void insert_debug_marker(std::string v) { debugMarker = std::move(v); }
}
extern "C" void aurora_pop_debug_group() { ++pops; }

static void require(bool v, const char* label) {
  ++checks; if (!v) throw std::runtime_error(label);
}
static bool close_float(float a, float b) { return std::abs(a - b) < 0.00001f; }
struct Wire {
  bool big;
  std::vector<u8> bytes;
  explicit Wire(bool be=true) : big(be) {}
  void u8v(u8 x) { bytes.push_back(x); }
  void scalar(u64 x, unsigned count) {
    for (unsigned i=0; i<count; ++i) bytes.push_back(x >> (8 * (big ? count - i - 1 : i)));
  }
  void u16v(u16 x) { scalar(x,2); }
  void u32v(u32 x) { scalar(x,4); }
  void u64v(u64 x) { scalar(x,8); }
  void f32(float x) { u32v(std::bit_cast<u32>(x)); }
  void bp(u32 x) { u8v(GX_LOAD_BP_REG); u32v(x); }
  void cp(u8 address, u32 x) { u8v(GX_LOAD_CP_REG); u8v(address); u32v(x); }
  void xf(u16 address, std::initializer_list<u32> values) {
    u8v(GX_LOAD_XF_REG); u32v(((static_cast<u32>(values.size())-1)<<16) | address);
    for (auto x:values) u32v(x);
  }
  void aurora(u16 op) { u8v(GX_LOAD_AURORA); u16v(op); }
  void run() { fifo::process(bytes.data(), static_cast<u32>(bytes.size()), big); bytes.clear(); }
};
static void reset() {
  gx::register_state() = gx::GXRegisterState{};
  fifo::reset_cp_register_cache();
  shadow = {};
  for (u32 i=0;i<16;++i) { shadow.tevc[i]=(0xc0u+i*2)<<24; shadow.teva[i]=(0xc1u+i*2)<<24; }
  shadow.cmode0=0x41000000; shadow.zmode=0x40000000;
  shadow.suScis0=0x20000000; shadow.suScis1=0x21000000;
}
static void producer_roundtrip() {
  reset();
  GXSetTevOp(GX_TEVSTAGE0, GX_MODULATE);
  GXSetTevColorS10(GX_TEVREG1, {-1024,-1,1023,1});
  GXSetTevKColor(GX_KCOLOR3, {18,52,86,120});
  GXSetAlphaCompare(GX_LEQUAL, 18, GX_AOP_XOR, GX_GEQUAL, 171);
  GXSetZMode(true,GX_LEQUAL,true);
  GXSetBlendMode(GX_BM_BLEND,GX_BL_SRCALPHA,GX_BL_INVSRCALPHA,GX_LO_COPY);
  GXSetScissor(0,0,640,480);
  GXLightObj light{};
  GXInitLightAttn(&light,1,0,0,1,0,0);
  GXInitLightColor(&light,{18,52,86,120});
  GXInitLightPos(&light,1,-2,3); GXInitLightDir(&light,0,0,-1);
  GXLoadLightObjImm(&light,GX_LIGHT2);
  fifo::drain();
  auto& s=gx::register_state(); auto& t=s.tevStages[0];
  require(t.colorPass.b==GX_CC_TEXC && t.colorPass.c==GX_CC_RASC && t.colorOp.clamp,"TEV color roundtrip");
  require(t.alphaPass.b==GX_CA_TEXA && t.alphaPass.c==GX_CA_RASA && t.alphaOp.op==GX_TEV_ADD,"TEV alpha roundtrip");
  require(close_float(s.colorRegs[2][0],-1024.f/255.f) && close_float(s.colorRegs[2][1],-1.f/255.f) && close_float(s.colorRegs[2][2],1023.f/255.f),"signed color roundtrip");
  require(close_float(s.kcolors[3][3],120.f/255.f),"konst alpha roundtrip");
  require(s.alphaCompare.comp0==GX_LEQUAL && s.alphaCompare.comp1==GX_GEQUAL && s.alphaCompare.ref1==171 && s.alphaCompare.op==GX_AOP_XOR,"alpha compare roundtrip");
  require(s.depthCompare && s.depthUpdate && s.depthFunc==GX_LEQUAL,"depth roundtrip");
  require(s.blendMode==GX_BM_BLEND && s.blendFacSrc==GX_BL_SRCALPHA && s.blendFacDst==GX_BL_INVSRCALPHA,"blend roundtrip");
  require(s.logicalScissor==aurora::gfx::ClipRect{0,0,640,480},"scissor decode callback");
  require(s.lights[2].pos[0]==1 && s.lights[2].pos[1]==-2 && s.lights[2].dir[2]==1 && close_float(s.lights[2].color[0],18.f/255.f),"light bulk decode");
  require(s.preparedLightsDirty && waits==1,"FIFO synchronization and light invalidation");
}
static void register_banks(bool be) {
  reset(); auto& s=gx::register_state(); Wire w(be);
  w.bp(0x40000017); w.run();
  const auto epoch=s.pipelineStateGeneration;
  w.bp(0x40000017); w.run(); require(s.pipelineStateGeneration==epoch,"BP duplicate cache");
  w.bp(0xfe000010); w.bp(0x40000000); w.run();
  require(s.depthCompare && !s.depthUpdate && s.depthFunc==GX_LEQUAL,"BP one-shot mask preserves other fields");
  require(s.bpRegCache[0xfe]==0xffffff,"BP mask consumed");
  w.bp(0x4000001f); w.run(); require(s.depthUpdate && s.depthFunc==GX_ALWAYS,"following BP write full mask");
  w.cp(0x50,0x5a00); w.cp(0x60,1);
  w.cp(0x70,1u|(4u<<1)|(1u<<9)|(3u<<10));
  w.run();
  require(s.vtxDesc[GX_VA_POS]==GX_DIRECT && s.vtxDesc[GX_VA_NRM]==GX_INDEX16 && s.vtxDesc[GX_VA_CLR0]==GX_INDEX8 && s.vtxDesc[GX_VA_TEX0]==GX_DIRECT,"CP vertex descriptors");
  require(s.sourceVtxDesc==s.vtxDesc,"CP source descriptors");
  require(s.vtxFmts[0].attrs[GX_VA_POS].type==GX_F32 && s.vtxFmts[0].attrs[GX_VA_NRM].type==GX_S16 && s.vtxFmts[0].attrs[GX_VA_NRM].frac==14,"CP VAT formats");
  const auto cpEpoch=s.pipelineStateGeneration;
  w.cp(0x70,1u|(4u<<1)|(1u<<9)|(3u<<10)); w.run();
  require(s.pipelineStateGeneration==cpEpoch,"CP duplicate cache");
  w.xf(0x1020,{0x3f800000,0,0x40000000,0,0xbf800000,0xc0000000,GX_PERSPECTIVE});
  w.xf(0x101a,{0x43a00000,0xc3700000,0x4b7fffff,0x44250000,0x44110000,0x4b7fffff}); w.run();
  require(s.proj.m0[0]==1 && s.proj.m1[1]==2 && s.proj.m2[3]==-2 && s.proj.m3[2]==-1,"XF perspective projection");
  require(s.logicalViewport.left==0 && s.logicalViewport.top==0 && s.logicalViewport.width==640 && s.logicalViewport.height==480,"XF viewport reconstruction");
  w.xf(0,{0x3f800000,0,0,0x40000000,0,0x3f800000,0,0x40400000,0,0,0x3f800000,0x40800000}); w.run();
  require(s.pnMtx[0].pos.m0[3]==2 && s.pnMtx[0].pos.m1[3]==3 && s.pnMtx[0].pos.m2[3]==4,"XF position matrix");
  // Texture register bank 4 uses A0/A4/A8, not 84/88/8C.
  w.bp(0xa00000b9); w.bp(0xa4000300); w.bp(0xa840fcff); w.run();
  const auto& tex=s.loadedTextures[4];
  require(tex.width()==256 && tex.height()==64 && tex.format()==GX_TF_RGB565,"texture bank and dimension decoding");
  require(tex.wrap_s()==GX_REPEAT && tex.wrap_t()==GX_MIRROR && tex.mag_filter()==GX_LINEAR && tex.min_filter()==GX_LIN_MIP_NEAR,"texture sampler decoding");
}
static Wire texture_packet(bool be, u8 slot=4) {
  Wire w(be); w.aurora(GX_LOAD_AURORA_TEXOBJ); w.u8v(slot);
  w.u64v(0x1234567890ull); w.u32v(257); w.u32v(129); w.u32v(GX_TF_RGB565);
  w.u32v(GX_TLUT2); w.u8v(1); w.u32v(42); w.u32v(7); return w;
}
static void extensions(bool be) {
  reset(); auto& s=gx::register_state(); auto w=texture_packet(be); w.run();
  const auto& tex=s.loadedTextures[4];
  require(tex.width()==257 && tex.height()==129 && tex.data==reinterpret_cast<void*>(0x1234567890ull) && tex.texObjId==42 && tex.texDataVersion==7 && tex.has_mips(),"Aurora texture identity and full dimensions");
  w.aurora(GX_LOAD_AURORA_ARRAYBASE); w.u64v(0x12340000); w.u32v(192); w.u8v(0); w.cp(0xb0,12); w.run();
  require(s.arrays[GX_VA_POS].data==reinterpret_cast<void*>(0x12340000) && s.arrays[GX_VA_POS].size==192 && s.arrays[GX_VA_POS].stride==12 && !s.arrays[GX_VA_POS].le,"Aurora vertex array metadata");
  s.arrays[GX_VA_POS].cachedRange={32,192}; w.u8v(GX_CMD_INVL_VC); w.run();
  require(s.arrays[GX_VA_POS].cachedRange.size==0,"vertex upload invalidation");
  w.aurora(GX_LOAD_AURORA_DESTROY_TEXOBJ); w.u32v(42);
  w.aurora(GX_LOAD_AURORA_DESTROY_TLUT); w.u32v(7);
  w.aurora(GX_LOAD_AURORA_DESTROY_COPY_TEX); w.u64v(0xabcdef00);
  w.aurora(GX_LOAD_AURORA_INVALIDATE_TEX_ALL); w.run();
  require(evictedTexture==42 && evictedTlut==7 && evictedCopy==reinterpret_cast<void*>(0xabcdef00) && invalidates!=0,"resource callbacks retain identities");
  for (auto op:{GX_LOAD_AURORA_DEBUG_GROUP_PUSH,GX_LOAD_AURORA_DEBUG_MARKER_INSERT}) {
    w.aurora(op); w.u16v(3); w.u8v('G'); w.u8v('X'); w.u8v('!');
  }
  w.aurora(GX_LOAD_AURORA_DEBUG_GROUP_POP); w.run();
  require(debugGroup=="GX!" && debugMarker=="GX!" && pops!=0,"debug callbacks");
  const auto oldDraws=draws;
  w.u8v(GX_DRAW_TRIANGLES|GX_VTXFMT3); w.u16v(3); w.u32v(0x12345678);
  const auto expected=w.bytes; w.run();
  require(draws==oldDraws+1 && drawBytes==expected && drawBigEndian==be,"draw packet forwarded unchanged to renderer boundary");
}
// A read past a truncated packet hits PAGE_NOACCESS, including in Release.
static void guarded_run(std::span<const u8> bytes, bool be) {
  SYSTEM_INFO info{}; GetSystemInfo(&info);
  const size_t page=info.dwPageSize;
  auto* memory=static_cast<u8*>(VirtualAlloc(nullptr,page*2,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
  if (!memory) throw std::runtime_error("guard allocation");
  DWORD previous;
  if (!VirtualProtect(memory+page,page,PAGE_NOACCESS,&previous)) { VirtualFree(memory,0,MEM_RELEASE); throw std::runtime_error("guard protection"); }
  auto* start=memory+page-bytes.size();
  if (!bytes.empty()) std::memcpy(start,bytes.data(),bytes.size());
  fifo::process(start,static_cast<u32>(bytes.size()),be);
  VirtualFree(memory,0,MEM_RELEASE);
}
static void malformed(bool be) {
  std::vector<Wire> packets;
  Wire bp(be); bp.bp(0x40000017); packets.push_back(bp);
  Wire cp(be); cp.cp(0x50,0x5a00); packets.push_back(cp);
  Wire xf(be); xf.xf(0x1020,{0x3f800000,0,0x3f800000,0,0xbf800000,0xc0000000,0}); packets.push_back(xf);
  packets.push_back(texture_packet(be));
  Wire str(be); str.aurora(GX_LOAD_AURORA_DEBUG_MARKER_INSERT); str.u16v(3); str.u8v('a'); str.u8v('b'); str.u8v('c'); packets.push_back(str);
  for (const auto& packet:packets) {
    for (size_t n=0;n<packet.bytes.size();++n) {
      reset(); const auto epoch=gx::register_state().pipelineStateGeneration;
      guarded_run(std::span(packet.bytes).first(n),be);
      require(gx::register_state().pipelineStateGeneration==epoch && gx::register_state().loadedTextures[4].texObjId==0,"truncation rejected before affected state mutation");
    }
  }
  reset(); auto bad=texture_packet(be,255); bad.bp(0x40000017); bad.run();
  require(gx::register_state().bpRegCache[0x40]==0,"invalid texture slot stops dispatch");
  fifo::process(nullptr,10,be);
}

#ifdef MKW_GX_COPY_COMMAND_TEST
static void copy_commands() {
  reset(); auto& s=gx::register_state();
  GXSetDispCopySrc(7,13,640,480);GXSetDispCopyDst(640,480);fifo::drain();
  require(s.dispCopySrc==aurora::gfx::ClipRect{7,13,640,480},"display copy source roundtrip");
  require(s.dispCopyDstWidth==640 && s.dispCopyDstHeight==480,"display copy destination width roundtrip (640 must stay 640)");
  for(u16 width=16;width<=1024;width+=16){GXSetDispCopyDst(width,480);fifo::drain();
    require(s.dispCopyDstWidth==width,"aligned XFB stride decoded as exact pixels");}
  GXSetDispCopyDst(641,480);fifo::drain();require(s.dispCopyDstWidth==640,"XFB register represents stride in 16-pixel units");
  for(unsigned n=0;n<32;++n){GXColor c{u8(n*7),u8(255-n*5),u8(n*3),u8(128+n)};
    u32 depth=0xab000000u|n*0x10203u;GXSetCopyClear(c,depth);fifo::drain();
    require(close_float(s.clearColor[0],c.r/255.f)&&close_float(s.clearColor[1],c.g/255.f)&&
      close_float(s.clearColor[2],c.b/255.f)&&close_float(s.clearColor[3],c.a/255.f),"clear RGBA survives FIFO");
    require(s.clearDepth==(depth&0xffffffu),"clear depth keeps only 24 bits");
  }
  u8 samples[12][2];u8 coefficients[7]={1,2,3,4,5,6,7};
  for(unsigned i=0;i<12;++i){samples[i][0]=u8(i);samples[i][1]=u8(15-i);}
  GXSetCopyFilter(GX_TRUE,samples,GX_TRUE,coefficients);fifo::drain();
  for(unsigned i=0;i<12;++i)for(unsigned j=0;j<2;++j)
    require(s.copyFilterSamplePattern[i][j]==samples[i][j],"AA pattern producer/decoder order");
  for(unsigned i=0;i<7;++i)require(s.copyFilterVFilter[i]==coefficients[i],"vertical filter coefficient order");
  GXSetCopyFilter(GX_FALSE,nullptr,GX_FALSE,nullptr);fifo::drain();
  for(const auto& sample:s.copyFilterSamplePattern)require(sample[0]==6&&sample[1]==6,"disabled AA default positions");
  require(s.copyFilterVFilter==std::array<u8,7>{0,0,21,22,21,0,0},"disabled vertical filter defaults");
  GXSetCopyClamp(GX_CLAMP_BOTTOM);GXSetDispCopyGamma(GX_GM_2_2);GXSetDispCopyFrame2Field(6);
  require(s.copyClamp==GX_CLAMP_BOTTOM&&s.dispCopyGamma==GX_GM_2_2&&s.dispCopyFrame2Field==2,"copy control setters");
  GXSetTexCopySrc(11,17,256,128);GXSetTexCopyDst(128,64,GX_TF_RGB565,GX_TRUE);
  require(s.texCopySrc==aurora::gfx::ClipRect{11,17,256,128}&&!s.texCopySrcRenderSpace,"logical texture copy source");
  require(s.texCopyDstWidth==128&&s.texCopyDstHeight==64&&s.texCopyFmt==GX_TF_RGB565&&s.texCopyHalfScale,"texture copy destination and half-scale");
  GXClearBoundingBox();fifo::drain();u16 l,r,t,b;GXReadBoundingBox(&l,&r,&t,&b);
  require(l==1023&&r==0&&t==1023&&b==0,"bounding-box clear through BP");
  GXReadBoundingBox(nullptr,nullptr,nullptr,nullptr);
  GXSetDispCopySrc(0,0,640,480);require(GXSetDispCopyYScale(1.f)==480,"unity display copy scale returns 480 lines");fifo::drain();
  require(s.dispCopyYScale==1.f,"unity y-scale register roundtrip");
  require(GXGetNumXfbLines(240,2.f)==479,"double scale spans source endpoints");
  require(GXGetNumXfbLines(480,2.f)==959,"double scale 480 lines");
  require(GXGetNumXfbLines(1024,2.f)==1024,"XFB output height clamps at 1024");
  for(u16 height:{u16(240),u16(480),u16(528),u16(576)})
    require(GXGetYScaleFactor(height,height)==1.f,"identity Y scale factor");
  for(float invalid:{0.f,-1.f,std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN(),
      std::numeric_limits<float>::min(),.5f,257.f}) {
    bool rejected=false;try{GXGetNumXfbLines(480,invalid);}catch(const std::invalid_argument&){rejected=true;}
    require(rejected,"invalid or zero-encoded Y scale rejected");
  }
  require(GXNtsc480IntDf.fbWidth==640&&GXNtsc480IntDf.efbHeight==480&&GXNtsc480IntDf.vfilter[3]==12,"requested render mode data");
}
#endif

int main() {
  try {
    aurora::g_config.logCallback=[](AuroraLogLevel,const char*,const char*,unsigned) {};
    fifo::init(); producer_roundtrip();
#ifdef MKW_GX_COPY_COMMAND_TEST
    copy_commands();
#endif
    for (bool be:{true,false}) { register_banks(be); extensions(be); malformed(be); }
    std::free(fifo::detail::sBufferData); fifo::detail::sBufferData=nullptr;
    std::printf("PASS GX registers: %u checks; real FIFO/BP/CP/XF, producer roundtrip, both byte orders, guarded truncations; renderer callbacks observed only\n",checks);
    return 0;
  } catch(const std::exception& error) { std::fprintf(stderr,"FAIL GX registers: %s\n",error.what()); return 1; }
}
