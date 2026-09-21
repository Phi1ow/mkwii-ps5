// SPDX-License-Identifier: GPL-3.0-only
// Native packet-path diagnostic. CPU decoding adapts this one test quad to the
// provided mesh VS; production rendering will fetch the retained raw buffers.
#include "gx_draw_backend.h"
#include "gx_geometry_buffer.h"
#include "gx_transform_state.h"
#include "gx_lighting_state.h"
#include "gx_texgen_state.h"
#include "gx/register_backend.hpp"
#include "gx/command_processor.hpp"
#include "dolphin/gx/__gx.h"
#include <memory>
#include <stdexcept>
#include <cstring>
#include <bit>
extern "C" void mkw_diagnostic_log(const char*);
namespace gx=aurora::gx;
namespace {
std::unique_ptr<mkw::agc::GxGeometry> geometry;
std::span<const uint8_t> positions,coordinates;
#ifdef MKW_GX_TRANSFORMS
mkw::agc::GxTransformState transforms;
bool transformsReady=false;
#ifdef MKW_GX_LIGHTING
MkwLighting lighting;
#ifdef MKW_GX_TEXGEN
MkwTexgenState texgen;
#endif
#endif
#endif
void put(std::vector<uint8_t>& out,uint32_t v,unsigned width,bool little=false){for(unsigned i=0;i<width;++i)out.push_back(v>>(8*(little?i:width-i-1)));}
}
// submit_raw_draw is linked but not executed by this FIFO-only diagnostic.
// Do not pull GXInit/VI into the isolated probe or invent a successful flush.
extern "C" void __GXSetDirtyState(){throw std::runtime_error("Unexpected dirty flush in geometry diagnostic");}
namespace mkw::agc {
static std::span<const uint8_t> probe_resolve_gx_array(void*,GXAttr attr,const gx::AttrArray& array,uint32_t offset,uint32_t bytes) {
    auto source=attr==GX_VA_POS?positions:attr==GX_VA_TEX0?coordinates:std::span<const uint8_t>{};
    if(array.data!=source.data() || offset>source.size() || bytes>source.size()-offset)
        throw std::runtime_error("Invalid diagnostic vertex array range");
    return source.subspan(offset,bytes);
}
static void probe_submit_gx_geometry(void*,GxGeometry&& packet,const gx::GXRegisterState& state) {
    if(geometry)throw std::runtime_error("More than one diagnostic draw");
    geometry=std::make_unique<GxGeometry>(std::move(packet));
#ifdef MKW_GX_TRANSFORMS
    transforms=GxTransformState::snapshot(state);transformsReady=true;
#ifdef MKW_GX_LIGHTING
    lighting=snapshot_gx_lighting(state);
#ifdef MKW_GX_TEXGEN
    texgen=snapshot_gx_texgen(state,
#ifdef MKW_GX_VARYINGS
        255
#else
        1
#endif
    );
#endif
#endif
#endif
}
}
#ifdef MKW_GX_TRANSFORMS
extern "C" int mkw_geometry_transforms(void* buffer,unsigned long capacity) {
    if(!transformsReady || capacity<sizeof(transforms))return 1;
    std::memcpy(buffer,&transforms,sizeof(transforms));
#ifdef MKW_GX_LIGHTING
    if(capacity<sizeof(transforms)+sizeof(lighting))return 1;
    std::memcpy(static_cast<uint8_t*>(buffer)+sizeof(transforms),&lighting,sizeof(lighting));
#ifdef MKW_GX_TEXGEN
    static_assert(sizeof(transforms)+sizeof(lighting)==MKW_GX_TEXGEN_OFFSET);
    if(capacity<MKW_GX_TEXGEN_OFFSET+sizeof(texgen))return 1;
    std::memcpy(static_cast<uint8_t*>(buffer)+MKW_GX_TEXGEN_OFFSET,&texgen,sizeof(texgen));
#endif
#endif
    return 0;
}
#endif
#ifdef MKW_RAW_VERTEX
extern "C" int mkw_geometry_raw(void* buffer,unsigned long capacity,unsigned long* used,unsigned int* indices) {
#else
extern "C" int mkw_geometry_quad(float* xyz,float* uv,unsigned int* colors,unsigned int* indices) {
#endif
    try {
        mkw::agc::ScopedGxDrawSink receiver({mkw::agc::probe_resolve_gx_array,mkw::agc::probe_submit_gx_geometry,nullptr});
        if(geometry)return 1;
        std::vector<uint8_t> pos(4*9,0),tex(251*5,0);
        const unsigned positionIndex[]={2,0,3,1},texIndex[]={250,3,51,120};
        for(unsigned v=0;v<4;++v) {
            std::vector<uint8_t> p,t;
            put(p,uint16_t(v==1 || v==2?24576:-24576),2);put(p,uint16_t(v>=2?-24576:24576),2);put(p,16384,2);
            put(t,v==1 || v==2?32768:0,2,true);put(t,v>=2?32768:0,2,true);
            std::memcpy(pos.data()+positionIndex[v]*9,p.data(),p.size());
            std::memcpy(tex.data()+texIndex[v]*5,t.data(),t.size());
        }
        positions=pos;coordinates=tex;
        auto& state=gx::register_state();state.currentPnMtx=0;state.cullMode=GX_CULL_NONE;
        state.arrays[GX_VA_POS]={pos.data(),uint32_t(pos.size()),9,false,{}};
        state.arrays[GX_VA_TEX0]={tex.data(),uint32_t(tex.size()),5,true,{}};
        std::vector<uint8_t> commands;
        auto cp=[&](uint8_t addr,uint32_t value){commands.push_back(8);commands.push_back(addr);put(commands,value,4);};
        unsigned matrixDescriptor=0;
#ifdef MKW_GX_TRANSFORMS
        matrixDescriptor=1;
#ifdef MKW_CURRENT_MATRIX
        matrixDescriptor=0;cp(0x30,57); // Current PN matrix slot 19, no vertex matrix bytes.
#endif
        // Four different slots span position and texture XF matrix memory.
        // Every transform maps its input to the established quad, but includes
        // nonidentity scale, cross terms, translation and negative camera Z.
        const unsigned matrixSlot[]={2,9,10,19};
        auto xf=[&](unsigned addr,std::span<const uint32_t> words){
            commands.push_back(0x10);put(commands,((words.size()-1)<<16)|addr,4);
            for(auto value:words)put(commands,value,4);
        };
        for(unsigned v=0;v<4;++v) {
#ifdef MKW_CURRENT_MATRIX
            if(v!=0)continue;
            const float rows[]={.5f,0,0,.25f, 0,2,0,-.5f, 0,0,-2,-1};
#else
            const float x=v==1 || v==2?.75f:-.75f,y=v>=2?-.75f:.75f;
            const float rows[]={.5f,.25f,0,.5f*x-.25f*y, -.25f,2,0,.25f*x-y, 0,0,-2,-1};
#endif
            std::array<uint32_t,12> words;
            for(unsigned i=0;i<12;++i)words[i]=std::bit_cast<uint32_t>(rows[i]);
#ifdef MKW_CURRENT_MATRIX
            xf(19*12,words);
#else
            xf(matrixSlot[v]*12,words);
#endif
        }
        // Perspective x/y factor 2; w=-cameraZ=2. Reversed-Z correction
        // changes the raw constant -1 into clip Z=1 (NDC 0.5).
        std::array<uint32_t,7> projection;
#ifdef MKW_CURRENT_MATRIX
        const float params[]={2,-.5f,.5f,.25f,.25f,0};
        projection[6]=GX_ORTHOGRAPHIC;
        state.renderViewport.znear=1;state.renderViewport.zfar=0;
#else
        const float params[]={2,0,2,0,0,-1};
        projection[6]=GX_PERSPECTIVE;
        state.renderViewport.znear=0;state.renderViewport.zfar=1;
#endif
        for(unsigned i=0;i<6;++i)projection[i]=std::bit_cast<uint32_t>(params[i]);
        xf(0x1020,projection);
        state.renderViewport.width=1920;state.renderViewport.height=1080;
#endif
        unsigned normalDescriptor=0,normalFormat=0,normalBytes=0;
#ifdef MKW_GX_LIGHTING
        normalDescriptor=GX_DIRECT<<11;normalFormat=GX_S16<<10;normalBytes=6;
        const float normalRows[]={0,2,0, 0,0,3, 4,0,0};
        std::array<uint32_t,9> normalWords;
        for(unsigned i=0;i<9;++i)normalWords[i]=std::bit_cast<uint32_t>(normalRows[i]);
        xf(0x400+9*9,normalWords);
        state.lights={};state.colorChannelState={};state.colorChannelConfig={};
        state.lights[0].pos={0,0,0,0};state.lights[0].dir={0,0,10,0};
        state.lights[0].cosAtt={0,1,0,0};state.lights[0].distAtt={1,0,0,0};
        state.lights[0].color={128.f/255,64.f/255,32.f/255,0};
        state.lights[1].color={0,0,0,64.f/255};
        auto& rgb=state.colorChannelConfig[0];rgb.lightingEnabled=true;rgb.diffFn=GX_DF_NONE;rgb.attnFn=GX_AF_SPEC;
        auto& alpha=state.colorChannelConfig[2];alpha.lightingEnabled=true;alpha.diffFn=GX_DF_NONE;alpha.attnFn=GX_AF_NONE;
        state.colorChannelState[0].matColor={1,1,1,1};state.colorChannelState[0].lightMask=1;
        state.colorChannelState[2].matColor={1,1,1,1};state.colorChannelState[2].lightMask=2;
#ifdef MKW_GX_VARYINGS
        state.colorChannelState[1].matColor={64.f/255,128.f/255,192.f/255,1};
        state.colorChannelState[3].matColor={1,1,1,160.f/255};
#endif
#endif
#ifdef MKW_GX_TEXGEN
        const float textureRows[]={2,0,0,1.5f, 0,-2,0,1.5f, 0,0,0,3};
        const float postRows[]={2,0,0,0, 0,2,0,0, 0,0,2,0};
        std::array<uint32_t,12> textureWords,postWords;
        for(unsigned i=0;i<12;++i){textureWords[i]=std::bit_cast<uint32_t>(textureRows[i]);postWords[i]=std::bit_cast<uint32_t>(postRows[i]);}
        xf(10*12,textureWords);xf(0x500+19*12,postWords);
        cp(0x30,57|(30<<6)); // Current position 19, TEX0 matrix slot 10.
        const std::array<uint32_t,1> num{1},dual{1},generator{(1u<<1)|(1u<<2)},post{57};
        xf(0x103f,num);xf(0x1012,dual);xf(0x1040,generator);xf(0x1050,post);
#ifdef MKW_GX_VARYINGS
        // Eight distinct rotations/reflections of the source coordinates.
        // Slot collapse or permutation changes the sampled quadrant pattern.
        uint32_t packed0=57,packed1=0;
        for(unsigned n=0;n<8;++n) {
            float rows[12]={0,0,0,1.5f,0,0,0,1.5f,0,0,0,3};
            const bool swap=n&4,flipS=n&1,flipT=n&2;
            rows[swap?1:0]=(swap?-2.f:2.f)*(flipS?-1.f:1.f);
            rows[4+(swap?0:1)]=(swap?2.f:-2.f)*(flipT?-1.f:1.f);
            for(unsigned j=0;j<12;++j)textureWords[j]=std::bit_cast<uint32_t>(rows[j]);
            xf((10+n)*12,textureWords);xf(0x1040+n,generator);xf(0x1050+n,post);
            if(n<4)packed0|=((10+n)*3)<<(6+6*n);
            else packed1|=((10+n)*3)<<(6*(n-4));
        }
        cp(0x30,packed0);cp(0x40,packed1);
        const std::array<uint32_t,1> eight{8};xf(0x103f,eight);
#endif
#endif
        cp(0x50,matrixDescriptor|normalDescriptor|(GX_INDEX8<<9)|(GX_DIRECT<<13));cp(0x60,GX_INDEX16);
        cp(0x70,1u|(GX_S16<<1)|(15u<<4)|normalFormat|(GX_RGB565<<14)|(1u<<21)|(GX_U16<<22)|(15u<<25));
        commands.push_back(0x80);put(commands,4,2);
        for(unsigned v=0;v<4;++v){
#ifdef MKW_GX_TRANSFORMS
            if(matrixDescriptor)commands.push_back(matrixSlot[v]*3);
#endif
            commands.push_back(positionIndex[v]);
#ifdef MKW_GX_LIGHTING
            put(commands,8192,2);put(commands,0,2);put(commands,0,2);
#endif
            put(commands,0xffff,2);put(commands,texIndex[v],2);
        }
        gx::fifo::process(commands.data(),uint32_t(commands.size()),true);
        if(!geometry || geometry->vertex_count()!=4 || geometry->indices().size()!=6 || geometry->layout().stride!=5+matrixDescriptor+normalBytes)return 2;
        if(geometry->arrays()[GX_VA_POS].bytes.size()!=33 || geometry->arrays()[GX_VA_TEX0].sourceOffset!=15 || geometry->arrays()[GX_VA_TEX0].bytes.size()!=1239)return 3;
        // Destroy every source before reading the packet for upload.
        std::fill(pos.begin(),pos.end(),0);std::fill(tex.begin(),tex.end(),0);std::fill(commands.begin(),commands.end(),0);
#ifdef MKW_GX_TRANSFORMS
        state.pnMtx={};state.texMtxs={};state.proj={};
#ifdef MKW_GX_LIGHTING
        state.lights={};state.colorChannelState={};
        mkw_diagnostic_log("[mkw-lighting] S16 normal and XF normal matrix 9; independent SPEC RGB and NONE alpha snapshots; live state destroyed\n");
#endif
#ifdef MKW_GX_TEXGEN
        state.ptTexMtxs={};state.tcgs={};state.dualTex=0;
        mkw_diagnostic_log("[mkw-texgen] XF source POS, 3x4 texture matrix 10 and post matrix 19; generated Q=6; mutable state destroyed\n");
#ifdef MKW_GX_VARYINGS
        mkw_diagnostic_log("[mkw-varyings] eight distinct XF transforms 10..17, used mask 255; second raster uses independent register materials\n");
#endif
#endif
#ifdef MKW_CURRENT_MATRIX
        if(geometry->current_position_matrix()!=19)return 6;
        mkw_diagnostic_log("[mkw-transform] captured CP current matrix 19, XF orthographic projection and reversed viewport range; no per-vertex matrix bytes; live matrices destroyed\n");
#else
        mkw_diagnostic_log("[mkw-transform] captured real XF matrix slots 2,9,10,19 and perspective projection; live matrices destroyed before GPU upload\n");
#endif
#endif
#ifdef MKW_RAW_VERTEX
        auto raw=mkw::agc::serialize_gx_geometry(*geometry);
        if(raw.size()>capacity)return 4;
        std::memcpy(buffer,raw.data(),raw.size());*used=raw.size();
#else
        for(unsigned v=0;v<4;++v) {
            const auto decoded=mkw::agc::decode_gx_vertex(*geometry,v);
            const float expectedX=v==1 || v==2?.75f:-.75f,expectedY=v>=2?-.75f:.75f;
            if(decoded.position[0]!=expectedX || decoded.position[1]!=expectedY || decoded.position[2]!=.5f ||
                decoded.uv[0][0]!=(v==1 || v==2?1.f:0.f) || decoded.uv[0][1]!=(v>=2?1.f:0.f) ||
                decoded.colors[0]!=std::array<float,4>{1,1,1,1} || decoded.positionMatrix!=0)return 4;
            std::memcpy(xyz+v*3,decoded.position.data(),3*sizeof(float));
            std::memcpy(uv+v*2,decoded.uv[0].data(),2*sizeof(float));colors[v]=0xffffffffu;
        }
#endif
        std::memcpy(indices,geometry->indices().data(),6*sizeof(uint32_t));
        geometry.reset();positions={};coordinates={};
#ifdef MKW_RAW_VERTEX
        mkw_diagnostic_log("[mkw-geometry] raw GPU upload prepared: actual CP/FIFO indexed S16 XYZ, little-endian U16 UV, RGB565; sources mutated, no CPU vertex decode\n");
#else
        mkw_diagnostic_log("[mkw-geometry] PASS actual CP/FIFO quad: four indexed S16 XYZ, indexed little-endian U16 UV, direct RGB565 colors, six triangle indices; snapshots survived source mutation\n");
#endif
        return 0;
    }catch(const std::exception& e){mkw_diagnostic_log("[mkw-geometry] FAIL ");mkw_diagnostic_log(e.what());mkw_diagnostic_log("\n");return 5;}
}
