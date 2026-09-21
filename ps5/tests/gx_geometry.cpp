// SPDX-License-Identifier: GPL-3.0-only
// Real FIFO/CP decoder and geometry packet path. The renderer endpoint only
// captures immutable packets for inspection; no GPU rendering is claimed.
#include "gx_draw_backend.h"
#include "gx_geometry_buffer.h"
#include "gx_transform_state.h"
#include "gx/register_backend.hpp"
#include "gx/command_processor.hpp"
#include "gx/register_decoder.hpp"
#include "dolphin/gx/frontend.hpp"
#include "dolphin/gx/__gx.h"
#include <aurora/gfx.h>
#include <dolphin/gx/GXAurora.h>
#include <bit>
#include <cstdio>
#include <map>
#include <exception>
#include <stdexcept>
namespace gx=aurora::gx;
namespace fifo=aurora::gx::fifo;
using namespace mkw::agc;
static unsigned checks,resolves;
static void check(bool v,const char* why){++checks;if(!v)throw std::runtime_error(why);}
template<class F>static void rejects(F f){bool caught=false;try{f();}catch(const std::exception&){caught=true;}check(caught,"malformed geometry accepted");}
// Execute the actual C shader on the host. Only the five GPU memory/export
// intrinsics are replaced; attribute decoding and transform arithmetic are
// compiled unchanged. This checks the source algorithm, not AMD instructions.
using ShaderU4=unsigned __attribute__((ext_vector_type(4)));
using ShaderF4=float __attribute__((ext_vector_type(4)));
static std::array<std::vector<uint8_t>,2> shaderBuffers;
static std::array<std::array<float,4>,34> shaderExports{};
static unsigned shaderCases;
extern "C" unsigned load_word(ShaderU4 d,unsigned at,unsigned scalar,unsigned) {
    const auto& b=shaderBuffers.at(d[0]);size_t off=size_t(at)+scalar;
    if(off>b.size() || 4>b.size()-off)throw std::out_of_range("shader read outside owned buffer");
    uint32_t value;std::memcpy(&value,b.data()+off,4);return value;
}
extern "C" ShaderU4 load_meta(ShaderU4 d,unsigned at,unsigned) {
    return {load_word(d,at,0,0),load_word(d,at+4,0,0),load_word(d,at+8,0,0),load_word(d,at+12,0,0)};
}
extern "C" ShaderF4 load_uniform(ShaderU4 d,unsigned at,unsigned) {
    auto v=load_meta(d,at,0);return {std::bit_cast<float>(v[0]),std::bit_cast<float>(v[1]),std::bit_cast<float>(v[2]),std::bit_cast<float>(v[3])};
}
extern "C" ShaderF4 load_vector(ShaderU4 d,unsigned at,unsigned scalar,unsigned){return load_uniform(d,at+scalar,0);}
extern "C" void export_vertex(unsigned target,unsigned,float x,float y,float z,float w,bool,bool){shaderExports.at(target)={x,y,z,w};}
extern "C" void mkw_test_vertex_attribute(ShaderU4,unsigned,unsigned,unsigned,float*);
extern "C" void gx_vertex(ShaderU4,ShaderU4,unsigned);
static std::array<float,4> shader_attribute(const GxGeometry& g,unsigned attr,unsigned group=0) {
    shaderBuffers[0]=serialize_gx_geometry(g);std::array<float,4> out;
    mkw_test_vertex_attribute(ShaderU4{0,0,0,0},0,attr,group,out.data());++shaderCases;return out;
}
static __GXData_struct shadow{};
__GXData_struct* __gx=&shadow;
extern "C" void __GXSetDirtyState(){throw std::runtime_error("Dirty producer flushing is outside this isolated geometry test");}
namespace aurora {AuroraConfig g_config{};std::chrono::nanoseconds wait_for_frame_worker_sealed() noexcept{return {};}}
namespace aurora::gx {
void set_logical_viewport(const gfx::Viewport&) noexcept{std::terminate();}
void set_logical_scissor(const gfx::ClipRect&) noexcept{std::terminate();}
void set_render_viewport(const gfx::Viewport&) noexcept{std::terminate();}
void set_render_scissor(const gfx::ClipRect&) noexcept{std::terminate();}
void evict_texture_object(u32) noexcept{std::terminate();}
void evict_tlut_object(u32) noexcept{std::terminate();}
void evict_copy_texture(const void*) noexcept{std::terminate();}
void invalidate_static_texture_cache() noexcept{}
}
// gx_draw.cpp samples draw timing through the PS5 wait service clock;
// timing is irrelevant to these geometry checks.
namespace mkw::agc { uint64_t gpu_clock_nanos() { return 0; } }
namespace aurora::gfx {void push_debug_group(std::string){std::terminate();}void insert_debug_marker(std::string){std::terminate();}}
extern "C" void aurora_pop_debug_group(){std::terminate();}
static std::vector<GxGeometry> submitted;
static std::map<const void*,std::span<const uint8_t>> mapped;
namespace mkw::agc {
std::span<const uint8_t> resolve_gx_array(GXAttr,const gx::AttrArray& a,uint32_t offset,uint32_t bytes) {
    ++resolves;auto data=mapped.at(a.data);
    if(offset>data.size() || bytes>data.size()-offset)throw std::invalid_argument("Actual memory mapping shorter than GX declaration");
    return data.subspan(offset,bytes);
}
void submit_gx_geometry(GxGeometry&& g,const gx::GXRegisterState&){submitted.push_back(std::move(g));}
}
static std::span<const uint8_t> resolve(void*,GXAttr a,const gx::AttrArray& array,uint32_t offset,uint32_t bytes){return resolve_gx_array(a,array,offset,bytes);}
static void put(std::vector<uint8_t>& out,uint32_t v,unsigned width,bool little=false){for(unsigned i=0;i<width;++i)out.push_back(v>>(8*(little?i:width-i-1)));}
static gx::GXRegisterState base() {
    gx::GXRegisterState s{};s.currentPnMtx=4;s.vtxDesc.fill(GX_NONE);
    s.vtxDesc[GX_VA_POS]=GX_DIRECT;s.vtxFmts[0].attrs[GX_VA_POS]={GX_POS_XY,GX_U8,0,0};return s;
}
static GxGeometry packet(const gx::GXRegisterState& s,std::span<const uint8_t> data,uint16_t count=1,GXPrimitive p=GX_POINTS){return GxGeometry::snapshot(s,p,GX_VTXFMT0,count,data,resolve,nullptr);}
static void number_formats() {
    unsigned cases=0;
    for(auto mode:{GX_DIRECT,GX_INDEX8,GX_INDEX16})for(unsigned type=0;type<=4;++type)
    for(unsigned frac:{0,1,6,7,14,31})for(unsigned components:{2,3})for(bool little:{false,true}) {
        auto s=base();s.vtxDesc[GX_VA_PNMTXIDX]=GX_DIRECT;s.vtxDesc[GX_VA_POS]=mode;
        s.vtxFmts[0].attrs[GX_VA_POS]={components==2?GX_POS_XY:GX_POS_XYZ,GXCompType(type),uint8_t(frac),0};
        unsigned width=type<2?1:type<4?2:4;
        std::array<uint32_t,3> raw=type==4?std::array<uint32_t,3>{0x3fa00000,0xc0100000,0x80000000}:
            width==1?std::array<uint32_t,3>{0x80,0xff,0x21}:std::array<uint32_t,3>{0x8123,0xfffe,0x1234};
        std::vector<uint8_t> records{27},values;
        for(unsigned c=0;c<components;++c)put(values,raw[c],width,mode==GX_DIRECT?false:little);
        std::vector<uint8_t> array;
        unsigned index=mode==GX_INDEX16?258:2;
        if(mode==GX_DIRECT)records.insert(records.end(),values.begin(),values.end());
        else {
            put(records,index,mode==GX_INDEX16?2:1);
            array.resize(index*17,0xcc);array.insert(array.end(),values.begin(),values.end());
            s.arrays[GX_VA_POS]={array.data(),uint32_t(array.size()),17,little,{}};mapped[array.data()]=array;
        }
        auto g=packet(s,records);auto v=decode_gx_vertex(g,0);auto sv=shader_attribute(g,GX_VA_POS);
        check(v.positionMatrix==9,"position matrix byte divides by three");
        for(unsigned c=0;c<components;++c) {
            float expected;
            if(type==4)expected=std::bit_cast<float>(raw[c]);
            else {
                int32_t signedValue=int32_t(raw[c]);
                if(type==GX_S8 && signedValue>=128)signedValue-=256;
                if(type==GX_S16 && signedValue>=32768)signedValue-=65536;
                expected=float(std::ldexp(double(signedValue),-int(frac)));
            }
            check(std::bit_cast<uint32_t>(v.position[c])==std::bit_cast<uint32_t>(expected),"numeric vertex format/endian/dequantization");
            check(std::bit_cast<uint32_t>(sv[c])==std::bit_cast<uint32_t>(expected),"actual shader numeric fetch matches independent expected bits");
        }
        if(components==2)check(v.position[2]==0,"XY pads Z with zero");
        if(mode!=GX_DIRECT)check(g.arrays()[GX_VA_POS].bytes.size()==components*width && g.arrays()[GX_VA_POS].sourceOffset==index*17,"array snapshot only referenced interval");
        ++cases;mapped.clear();
    }
    check(cases==360,"numeric format coverage");
}
static void colors_and_uv() {
    const unsigned widths[]={2,3,4,2,3,4};
    for(unsigned type=0;type<6;++type)for(bool little:{false,true})for(auto mode:{GX_DIRECT,GX_INDEX16}) {
        auto s=base();s.vtxDesc[GX_VA_CLR1]=mode;s.vtxFmts[0].attrs[GX_VA_CLR1]={GX_CLR_RGBA,GXCompType(type),0,0};
        s.vtxDesc[GX_VA_TEX7]=GX_DIRECT;s.vtxFmts[0].attrs[GX_VA_TEX7]={GX_TEX_S,GX_S16,6,0};
        uint32_t bits=type==GX_RGB565?0xad69:type==GX_RGBA4?0x5a3c:0xabc123;
        std::vector<uint8_t> bytes;
        if(type==GX_RGB8 || type==GX_RGBX8 || type==GX_RGBA8){bytes={17,83,231};if(widths[type]==4)bytes.push_back(67);}
        else put(bytes,bits,widths[type],mode==GX_DIRECT?false:little);
        std::vector<uint8_t> records{4,9};
        if(mode==GX_DIRECT)records.insert(records.end(),bytes.begin(),bytes.end());
        else {records.insert(records.end(),{0,0});s.arrays[GX_VA_CLR1]={bytes.data(),uint32_t(bytes.size()),0,little,{}};mapped[bytes.data()]=bytes;}
        put(records,0xffa0,2);auto g=packet(s,records);auto v=decode_gx_vertex(g,0);
        std::array<float,4> expected;
        if(type==GX_RGB565)expected={21.f/31,43.f/63,9.f/31,1};
        else if(type==GX_RGBA4)expected={5.f/15,10.f/15,3.f/15,12.f/15};
        else if(type==GX_RGBA6)expected={42.f/63,60.f/63,4.f/63,35.f/63};
        else expected={17.f/255,83.f/255,231.f/255,type==GX_RGBA8?67.f/255:1};
        check(v.colors[1]==expected,"packed color precision and byte order");
        check(v.colors[0]==std::array<float,4>{1,1,1,1},"missing colors default white");
        check(v.uv[7]==std::array<float,2>{-1.5f,0},"single signed texture coordinate with fraction");
        check(v.positionMatrix==4 && v.textureMatrices[0]==~0u,"matrix defaults preserve current state and absent tex override");
        auto shaderColor=shader_attribute(g,GX_VA_CLR1),shaderUv=shader_attribute(g,GX_VA_TEX7);
        for(unsigned c=0;c<4;++c)check(std::abs(shaderColor[c]-expected[c])<=1.2e-7f,"actual shader packed color within two float ulps");
        check(shaderUv[0]==-1.5f && shaderUv[1]==0,"actual shader single signed UV");
        check(shader_attribute(g,GX_VA_CLR0)==std::array<float,4>{1,1,1,1},"actual shader absent color");
        check(shader_attribute(g,GX_VA_NRM)==std::array<float,4>{1,0,0,0},"actual shader absent normal");
        mapped.clear();
    }
}
static void normals_and_lifetime() {
    for(auto count:{GX_NRM_XYZ,GX_NRM_NBT,GX_NRM_NBT3})for(auto mode:{GX_DIRECT,GX_INDEX8,GX_INDEX16})
    for(bool le:{false,true})for(auto type:{GX_U8,GX_S8,GX_U16,GX_S16,GX_F32}) {
        const bool unsignedType=type==GX_U8 || type==GX_U16;
        const unsigned width=type<2?1:type<4?2:4;
        const unsigned fraction=type==GX_U8?7:type==GX_S8?6:type==GX_U16?15:type==GX_S16?14:0;
        auto expectedValue=[&](unsigned c){return float(c+1)/8.f-(unsignedType?0.f:1.f);};
        auto encoded=[&](unsigned c){float v=expectedValue(c);return type==GX_F32?std::bit_cast<uint32_t>(v):uint32_t(int32_t(std::ldexp(v,fraction)));};
        auto s=base();s.vtxDesc[GX_VA_NRM]=mode;s.vtxFmts[0].attrs[GX_VA_NRM]={count,type,uint8_t(fraction),0};
        std::vector<uint8_t> records{1,2},array;
        const bool triple=count==GX_NRM_NBT3;const unsigned elements=count==GX_NRM_XYZ?3:9;
        if(mode==GX_DIRECT)for(unsigned c=0;c<elements;++c)put(records,encoded(c),width);
        else {
            array.resize(200,0);unsigned stride=triple?17:41;
            auto element=[&](unsigned at,unsigned c){std::vector<uint8_t> bytes;put(bytes,encoded(c),width,le);std::copy(bytes.begin(),bytes.end(),array.begin()+at);};
            if(triple){for(unsigned group=0;group<3;++group){unsigned index=3-group;put(records,index,mode==GX_INDEX16?2:1);for(unsigned c=0;c<3;++c)element(index*stride+c*width,group*3+c);}}
            else {put(records,2,mode==GX_INDEX16?2:1);for(unsigned c=0;c<elements;++c)element(2*stride+c*width,c);}
            s.arrays[GX_VA_NRM]={array.data(),uint32_t(array.size()),uint8_t(stride),le,{}};mapped[array.data()]=array;
        }
        auto g=packet(s,records);std::fill(records.begin(),records.end(),0);std::fill(array.begin(),array.end(),0);
        auto v=decode_gx_vertex(g,0);
        for(unsigned group=0;group<3;++group) {
            auto value=shader_attribute(g,GX_VA_NRM,group);
            const auto& decoded=group==0?v.normal:group==1?v.binormal:v.tangent;
            for(unsigned c=0;c<3;++c) {
                const auto expected=expectedValue((count==GX_NRM_XYZ?0:group*3)+c);
                check(decoded[c]==expected,"CPU NBT groups survive source mutation across numeric formats");
                check(value[c]==expected,"actual shader NBT group, byte order, fraction and independent index fetch");
            }
        }
        mapped.clear();
    }
}
static void primitives_and_bounds() {
    auto s=base();std::vector<uint8_t> bytes(12);
    check(packet(s,bytes,6,GX_TRIANGLESTRIP).indices()==std::vector<uint32_t>{0,1,2,2,1,3,2,3,4,4,3,5},"strip winding alternation");
    check(packet(s,bytes,6,GX_TRIANGLEFAN).indices()==std::vector<uint32_t>{0,1,2,0,2,3,0,3,4,0,4,5},"fan center");
    check(packet(s,bytes,6,GX_QUADS).indices()==std::vector<uint32_t>{0,1,2,2,3,0},"incomplete quad never emits missing vertices");
    check(packet(s,bytes,6,GX_LINES).indices()==std::vector<uint32_t>{0,1,2,3,4,5},"line endpoint pairs");
    check(packet(s,bytes,6,GX_LINESTRIP).indices()==std::vector<uint32_t>{0,1,1,2,2,3,3,4,4,5},"line strip segments");
    for(auto primitive:{GX_QUADS,GX_TRIANGLES,GX_TRIANGLESTRIP,GX_TRIANGLEFAN,GX_LINES,GX_LINESTRIP,GX_POINTS})
    for(unsigned count:{0,1,2,3,4,65535}) {
        std::vector<uint8_t> raw(count*2);auto g=packet(s,raw,uint16_t(count),primitive);
        check(std::all_of(g.indices().begin(),g.indices().end(),[&](auto i){return i<count;}),"generated index out of bounds");
    }
    rejects([&]{packet(s,bytes,5);});rejects([&]{GxVertexLayout::build(s,GXVtxFmt(8));});
    s.vtxDesc[GX_VA_POS]=GX_NONE;rejects([&]{packet(s,{},1);});s=base();s.vtxDesc[GX_VA_PNMTXIDX]=GX_INDEX8;rejects([&]{GxVertexLayout::build(s,GX_VTXFMT0);});
    s=base();s.vtxDesc[GX_VA_POS]=GX_INDEX16;std::vector<uint8_t> indices{0xff,0xff},array(8);
    s.arrays[GX_VA_POS]={array.data(),8,255,true,{}};mapped[array.data()]=array;
    auto calls=resolves;rejects([&]{packet(s,indices);});check(resolves==calls,"array bounds checked before resolver");
    s.arrays[GX_VA_POS].size=UINT32_MAX;rejects([&]{packet(s,indices);});
    s.arrays[GX_VA_POS].size=8;s.arrays[GX_VA_POS].stride=0;auto g=packet(s,indices);check(g.arrays()[GX_VA_POS].bytes.size()==2,"zero-stride array broadcasts element zero");
    rejects([&]{decode_gx_vertex(g,1);});mapped.clear();
    for(unsigned cmd=0x80;cmd<=0xbf;++cmd)check(unsigned(gx_primitive_from_command(cmd))==((cmd&0xf8)==0x88?0x80:(cmd&0xf8)),"primitive opcode/VAT split");
    rejects([]{gx_primitive_from_command(0x70);});
    // Merged commands: records concatenate, each segment keeps its own triangles offset by earlier vertices.
    s=base();const auto layout=GxVertexLayout::build(s,GX_VTXFMT0);std::vector<uint8_t> eight(16);
    const GxDrawSegment strips[]={{GX_TRIANGLESTRIP,4},{GX_QUADS,4},{GX_TRIANGLES,2}};
    std::vector<uint8_t> ten(20);
    auto merged=GxGeometry::snapshot_segments(layout,s,strips,ten,resolve,nullptr);
    check(merged.vertex_count()==10&&merged.primitive()==GX_TRIANGLES&&merged.records().size()==20,"merged records and count");
    check(merged.indices()==std::vector<uint32_t>{0,1,2,2,1,3,4,5,6,6,7,4},"merged segments offset their indices; incomplete tail emits none");
    const GxDrawSegment mixed[]={{GX_TRIANGLES,3},{GX_LINES,1}};
    rejects([&]{GxGeometry::snapshot_segments(layout,s,mixed,eight,resolve,nullptr);});
    rejects([&]{GxGeometry::snapshot_segments(layout,s,strips,eight,resolve,nullptr);});
}
static void gpu_buffer_abi() {
    auto s=base();s.vtxDesc[GX_VA_POS]=GX_INDEX16;
    s.vtxFmts[0].attrs[GX_VA_POS]={GX_POS_XYZ,GX_S16,7,0};
    std::vector<uint8_t> array(33,0xcc),records{0,3,0,1};
    const std::array<uint8_t,6> value{0x80,0xff,0x34,0x12,0x00,0x01};
    std::copy(value.begin(),value.end(),array.begin()+27);
    s.arrays[GX_VA_POS]={array.data(),uint32_t(array.size()),9,true,{}};mapped[array.data()]=array;
    auto g=packet(s,records,2);auto bytes=serialize_gx_geometry(g);
    std::fill(array.begin(),array.end(),0);std::fill(records.begin(),records.end(),0);
    auto word=[&](size_t at){uint32_t v=0;for(unsigned b=0;b<4;++b)v|=uint32_t(bytes.at(at+b))<<(b*8);return v;};
    check(word(0)==0x31565847 && word(4)==2 && word(12)==2 && word(16)==4,"GPU header magic/stride/count/current matrix");
    check(word(8)==704 && bytes.size()%4==0,"GPU byte offsets and alignment");
    check(bytes[704]==0 && bytes[705]==3 && bytes[706]==0 && bytes[707]==1,"GPU FIFO indices retain big endian");
    const size_t a=32+9*32;
    check(word(a)==(3u|(3u<<2)|(3u<<5)|(1u<<9)|(7u<<11)|(1u<<16)),"GPU attribute flags ABI");
    check(word(a+4)==0 && word(a+8)==9 && word(a+12)==9 && word(a+20)==24,"GPU indexed range/stride/source origin");
    const auto begin=word(a+16),last=begin+3*word(a+8)-word(a+12);
    check(begin==708 && std::equal(value.begin(),value.end(),bytes.begin()+last),"GPU indexed address reaches original little endian data");
    check((last+4)%4==2 && ((last+4)&~3u)+8<=bytes.size(),"unaligned final u16 has two owned aligned words");
    check(word(bytes.size()-4)==0,"GPU explicit zero tail");
    check((word(32+13*32)&3)==0 && word(32+13*32+16)==0,"absent GPU attributes have no data pointer");
    mapped.clear();
}
static void transform_snapshots() {
    auto& s=gx::register_state();s=gx::GXRegisterState{};
    std::vector<uint8_t> commands;
    auto xf=[&](unsigned addr,const std::vector<uint32_t>& words){
        commands.push_back(0x10);put(commands,((words.size()-1)<<16)|addr,4);
        for(auto w:words)put(commands,w,4);
    };
    for(unsigned slot=0;slot<20;++slot) {
        std::vector<uint32_t> words;
        for(unsigned n=0;n<12;++n)words.push_back(std::bit_cast<uint32_t>(float(slot*100+n)+.25f));
        xf(slot*12,words);
        for(auto& w:words)w=std::bit_cast<uint32_t>(-std::bit_cast<float>(w));
        xf(0x500+slot*12,words);
        if(slot<10) {
            words.clear();for(unsigned n=0;n<9;++n)words.push_back(std::bit_cast<uint32_t>(float(slot*10+n)+.5f));
            xf(0x400+slot*9,words);
        }
    }
    fifo::process(commands.data(),uint32_t(commands.size()),true);
    auto matrices=GxTransformState::snapshot(s);
    s.pnMtx={};s.texMtxs={};s.ptTexMtxs={};
    for(unsigned slot=0;slot<20;++slot)for(unsigned n=0;n<12;++n) {
        check(matrices.values[20+slot*12+n]==float(slot*100+n)+.25f,"XF position/texture shared rows survive state mutation");
        check(matrices.values[380+slot*12+n]==-(float(slot*100+n)+.25f),"XF post texture rows survive state mutation");
    }
    for(unsigned slot=0;slot<10;++slot)for(unsigned row=0;row<3;++row)for(unsigned col=0;col<4;++col)
        check(matrices.values[260+slot*12+row*4+col]==(col==3?0:float(slot*10+row*3+col)+.5f),"XF normal 3x3 expands to padded matrix rows");
    for(bool perspective:{false,true})for(bool reversedRange:{false,true}) {
        commands.clear();std::vector<uint32_t> words;
        for(float f:{2.f,.25f,3.f,-.5f,-.75f,-1.f})words.push_back(std::bit_cast<uint32_t>(f));
        words.push_back(perspective?GX_PERSPECTIVE:GX_ORTHOGRAPHIC);xf(0x1020,words);
        fifo::process(commands.data(),uint32_t(commands.size()),true);
        s.renderViewport.width=4;s.renderViewport.height=-2;
        s.renderViewport.znear=reversedRange?1:0;s.renderViewport.zfar=reversedRange?0:1;
        auto snapshot=GxTransformState::snapshot(s);const auto& p=snapshot.values;
        check(p[0]==2 && p[5]==3 && p[2]==(perspective?.25f:0) && p[3]==(perspective?0:.25f),"XF projection perspective/ortho X row");
        check(p[6]==(perspective?-.5f:0) && p[7]==(perspective?0:-.5f),"XF projection perspective/ortho Y row");
        check(p[14]==(perspective?-1:0) && p[15]==(perspective?0:1),"XF projection W row retained");
        check(p[10]==(reversedRange?(perspective?-1.75f:-.75f):.75f) &&
              p[11]==(reversedRange?(perspective?-1:0):1),"exactly one depth correction for either near/far ordering");
        check(p[16]==-1.f/24 && p[17]==1.f/12 && p[18]==4 && p[19]==-2,"GX pixel center correction and signed viewport retained");
        // Execute the complete, unchanged vertex entry against a non-symmetric
        // model matrix. Expected camera position is explicitly (14,42,70).
        auto draw=s;draw.vtxDesc.fill(GX_NONE);draw.vtxDesc[GX_VA_POS]=GX_DIRECT;
        draw.vtxFmts[0].attrs[GX_VA_POS]={GX_POS_XY,GX_U8,0,0};draw.currentPnMtx=4;
        draw.pnMtx[4].pos={{1,2,3,4},{5,6,7,8},{9,10,11,12}};
        const std::vector<uint8_t> vertex{2,4};auto geometry=packet(draw,vertex);
        auto uniform=GxTransformState::snapshot(draw);
        shaderBuffers[0]=serialize_gx_geometry(geometry);shaderBuffers[1].resize(sizeof(uniform));
        std::memcpy(shaderBuffers[1].data(),&uniform,sizeof(uniform));
        gx_vertex(ShaderU4{0,0,0,0},ShaderU4{1,0,0,0},0);
        const float w=perspective?-70.f:1.f;
        const std::array<float,4> expectedClip{
            (perspective?45.5f:28.25f)-w/24.f,(perspective?91.f:125.5f)+w/12.f,
            reversedRange?(perspective?-123.5f:-52.5f):53.5f,w};
        for(unsigned c=0;c<4;++c)check(std::abs(shaderExports[12][c]-expectedClip[c])<1e-5f,"actual vertex entry model/projection row convention");
        check(shaderExports[32]==std::array<float,4>{0,0,0,0} && shaderExports[33]==std::array<float,4>{1,1,1,1},"actual vertex entry defaults and export targets");
    }
    s.renderViewport.width=0;s.renderViewport.height=.5f;
    auto small=GxTransformState::snapshot(s);
    check(small.values[16]==-1.f/6 && small.values[17]==1.f/6,"viewport correction clamps small absolute dimensions");
}
static void fifo_roundtrip() {
    submitted.clear();gx::register_state()=gx::GXRegisterState{};fifo::reset_cp_register_cache();
    // CP VCD position direct; VAT0 XY/S16/frac2, PN matrix slot 3. Then draw.
    for(bool be:{false,true}) {
        std::vector<uint8_t> wire;
        auto cp=[&](uint8_t a,uint32_t v){wire.push_back(8);wire.push_back(a);put(wire,v,4,!be);};
        cp(0x50,1u<<9);cp(0x70,(GX_S16<<1)|(2u<<4));cp(0x30,9);
        wire.push_back(0x90);put(wire,3,2,!be);
        // Vertex ABI remains BE even in a little-endian command stream.
        for(unsigned i=0;i<3;++i){put(wire,0xfffcu+i*4,2);put(wire,8+i*4,2);}
        fifo::process(wire.data(),uint32_t(wire.size()),be);
        check(submitted.size()==(be?2:1),"real CP/FIFO submits draw packet");
        auto v=decode_gx_vertex(submitted.back(),0);check(v.position==std::array<float,3>{-1,2,0} && v.positionMatrix==3,"decoded CP state feeds actual geometry");
        std::fill(wire.begin(),wire.end(),0);check(decode_gx_vertex(submitted.back(),2).position[0]==1,"recorded draw owns FIFO bytes");
    }
    // Adjacent draws of one stream (NOP padding allowed) share a packet; any other command splits them.
    {
        std::vector<uint8_t> wire;
        auto cp=[&](uint8_t a,uint32_t v){wire.push_back(8);wire.push_back(a);put(wire,v,4,false);};
        auto draw=[&](uint8_t cmd){wire.push_back(cmd);put(wire,3,2,false);for(unsigned i=0;i<3;++i){put(wire,i*4,2);put(wire,i*4,2);}};
        cp(0x50,1u<<9);cp(0x70,(GX_S16<<1)|(2u<<4));
        auto start=submitted.size();
        draw(0x90);wire.push_back(0);draw(0x98);draw(0x90);
        fifo::process(wire.data(),uint32_t(wire.size()),true);
        check(submitted.size()==start+1&&submitted.back().vertex_count()==9&&submitted.back().indices().size()==9,"adjacent draws merged into one packet");
        check(submitted.back().indices()==std::vector<uint32_t>{0,1,2,3,4,5,6,7,8},"merged indices follow stream order");
        wire.clear();cp(0x50,1u<<9);cp(0x70,(GX_S16<<1)|(2u<<4));start=submitted.size();
        draw(0x90);cp(0x30,9);draw(0x90);
        fifo::process(wire.data(),uint32_t(wire.size()),true);
        check(submitted.size()==start+2&&submitted.back().current_position_matrix()==3,"a register write between draws splits them");
        wire.clear();cp(0x50,1u<<9);cp(0x70,(GX_S16<<1)|(2u<<4));start=submitted.size();
        cp(0x71,(GX_S16<<1)|(2u<<4));start=submitted.size();
        draw(0x90);draw(0x91);
        fifo::process(wire.data(),uint32_t(wire.size()),true);
        check(submitted.size()==start+2,"a draw with another vertex format is its own packet");
    }
    auto before=submitted.size();std::vector<uint8_t> trunc{0,3,0,0};u32 pos=0;
    check(!fifo::handle_draw(0x90,trunc.data(),pos,uint32_t(trunc.size()),true) && pos==0 && submitted.size()==before,"truncated draw is atomic");
    pos=UINT32_MAX;check(!fifo::handle_draw(0x90,trunc.data(),pos,2,true),"draw count bounds cannot wrap");
    gx::register_state().cullMode=GX_CULL_ALL;std::vector<uint8_t> draw(14);draw[1]=3;pos=0;
    check(fifo::handle_draw(0x90,draw.data(),pos,14,true) && pos==14 && submitted.size()==before,"culled draw consumed without submission");
    pos=0;check(fifo::handle_draw(0xb8,draw.data(),pos,14,true) && submitted.size()==before+1,"cull all does not cull points");
    gx::register_state().cullMode=GX_CULL_NONE;
    std::vector<uint8_t> raw(12);shadow.dirtyState=0;
    check(fifo::submit_raw_draw(GX_TRIANGLES,GX_VTXFMT0,raw.data(),3,12) && submitted.size()==before+2,"raw bridge submits same packet path");
    check(!fifo::submit_raw_draw(GX_TRIANGLES,GX_VTXFMT0,raw.data(),3,11),"raw bridge exact extent");
}
int main(){try{
    aurora::g_config.logCallback=[](AuroraLogLevel,const char*,const char*,unsigned){};fifo::init();
    number_formats();colors_and_uv();normals_and_lifetime();primitives_and_bounds();gpu_buffer_abi();transform_snapshots();fifo_roundtrip();
    std::printf("PASS GX geometry: %u checks; %u actual shader attribute calls on host; raw snapshots, numeric/color formats, NBT3, primitive indices, real CP/FIFO and raw bridge; no GPU rendering\n",checks,shaderCases);
    std::free(fifo::detail::sBufferData);fifo::detail::sBufferData=nullptr;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL GX geometry: %s\n",e.what());return 1;}}
