// SPDX-License-Identifier: GPL-3.0-only
#include "gx_geometry.h"
#include <algorithm>
#include <bit>
#include <limits>
#include <stdexcept>

namespace mkw::agc {
static void require(bool value,const char* why){if(!value)throw std::invalid_argument(why);}
static bool color(unsigned attr){return attr==GX_VA_CLR0 || attr==GX_VA_CLR1;}
static uint32_t load(std::span<const uint8_t> bytes,size_t at,unsigned width,bool little) {
    require(at<=bytes.size() && width<=bytes.size()-at,"GX attribute read outside snapshot");
    uint32_t v=0;for(unsigned i=0;i<width;++i)v|=uint32_t(bytes[at+i])<<(8*(little?i:width-i-1));return v;
}
GXPrimitive gx_primitive_from_command(uint8_t command) {
    switch(command&0xf8u) {
    case 0x80:case 0x88:return GX_QUADS;
    case 0x90:return GX_TRIANGLES;
    case 0x98:return GX_TRIANGLESTRIP;
    case 0xa0:return GX_TRIANGLEFAN;
    case 0xa8:return GX_LINES;
    case 0xb0:return GX_LINESTRIP;
    case 0xb8:return GX_POINTS;
    default:throw std::invalid_argument("Invalid GX primitive opcode");
    }
}
GxVertexLayout GxVertexLayout::build(const aurora::gx::GXRegisterState& state,GXVtxFmt format) {
    require(unsigned(format)<state.vtxFmts.size(),"GX VAT index out of range");
    GxVertexLayout out;
    for(unsigned attr=0;attr<GxGeometryAttributeCount;++attr) {
        const auto mode=state.vtxDesc[attr];auto& dst=out.attributes[attr];dst.mode=mode;
        if(mode==GX_NONE)continue;
        require(mode==GX_DIRECT || mode==GX_INDEX8 || mode==GX_INDEX16,"Invalid GX attribute mode");
        const auto& f=state.vtxFmts[format].attrs[attr];
        dst.offset=out.stride;dst.type=f.type;dst.fraction=f.frac;
        if(attr<GX_VA_POS) {
            require(mode==GX_DIRECT,"GX matrix index must be direct");
            dst.type=GX_U8;dst.fraction=0;dst.components=1;dst.componentBytes=1;
        }else if(color(attr)) {
            require(unsigned(f.type)<=GX_RGBA8,"Invalid GX color format");
            constexpr unsigned sizes[]={2,3,4,2,3,4};
            dst.components=1;dst.componentBytes=sizes[f.type];dst.fraction=0;
        }else {
            require(unsigned(f.type)<=GX_F32 && f.frac<=31,"Invalid GX numeric format");
            dst.componentBytes=f.type<=GX_S8?1:f.type<=GX_S16?2:4;
            if(attr==GX_VA_POS) {require(unsigned(f.cnt)<=GX_POS_XYZ,"Invalid GX position count");dst.components=f.cnt==GX_POS_XY?2:3;}
            else if(attr==GX_VA_NRM) {
                require(unsigned(f.cnt)<=GX_NRM_NBT3,"Invalid GX normal count");dst.components=f.cnt==GX_NRM_XYZ?3:9;
                dst.indexCount=f.cnt==GX_NRM_NBT3?3:1;
            }else {require(unsigned(f.cnt)<=GX_TEX_ST,"Invalid GX texture coordinate count");dst.components=f.cnt==GX_TEX_S?1:2;}
        }
        if(mode==GX_DIRECT)out.stride+=dst.componentBytes*dst.components;
        else {
            const auto& a=state.arrays[attr];dst.arrayStride=a.stride;dst.arrayLittleEndian=a.le;
            out.stride+=(mode==GX_INDEX16?2:1)*dst.indexCount;
        }
    }
    return out;
}
// Triangle-class indices of one primitive command, offset by `base`, written
// at `out` (which holds triangle_index_count(primitive,count) entries).
static uint32_t* write_triangle_indices(uint32_t* out,GXPrimitive primitive,uint32_t count,uint32_t base) {
    auto tri=[&](uint32_t a,uint32_t b,uint32_t c){out[0]=base+a;out[1]=base+b;out[2]=base+c;out+=3;};
    switch(primitive) {
    case GX_QUADS:for(uint32_t v=0;v+3<count;v+=4){tri(v,v+1,v+2);tri(v+2,v+3,v);}break;
    case GX_TRIANGLES:for(uint32_t v=0;v+2<count;v+=3)tri(v,v+1,v+2);break;
    case GX_TRIANGLESTRIP:for(uint32_t v=2;v<count;++v){if(v&1)tri(v-1,v-2,v);else tri(v-2,v-1,v);}break;
    case GX_TRIANGLEFAN:for(uint32_t v=2;v<count;++v)tri(0,v-1,v);break;
    default:throw std::invalid_argument("Only triangle GX primitives can share a draw");
    }
    return out;
}
static std::vector<uint32_t> build_indices(GXPrimitive primitive,uint32_t count) {
    std::vector<uint32_t> out;
    auto tri=[&](uint32_t a,uint32_t b,uint32_t c){out.push_back(a);out.push_back(b);out.push_back(c);};
    switch(primitive) {
    case GX_QUADS:
        out.reserve((count/4)*6);for(uint32_t v=0;v+3<count;v+=4){tri(v,v+1,v+2);tri(v+2,v+3,v);}break;
    case GX_TRIANGLES:
        out.reserve((count/3)*3);for(uint32_t v=0;v+2<count;v+=3)tri(v,v+1,v+2);break;
    case GX_TRIANGLESTRIP:
        out.reserve(count>2?(size_t(count)-2)*3:0);for(uint32_t v=2;v<count;++v){if(v&1)tri(v-1,v-2,v);else tri(v-2,v-1,v);}break;
    case GX_TRIANGLEFAN:
        out.reserve(count>2?(size_t(count)-2)*3:0);for(uint32_t v=2;v<count;++v)tri(0,v-1,v);break;
    case GX_LINES:
        out.reserve((count/2)*2);for(uint32_t v=0;v+1<count;v+=2){out.push_back(v);out.push_back(v+1);}break;
    case GX_LINESTRIP:
        out.reserve(count>1?(size_t(count)-1)*2:0);for(uint32_t v=1;v<count;++v){out.push_back(v-1);out.push_back(v);}break;
    case GX_POINTS:
        out.reserve(count);for(uint32_t v=0;v<count;++v)out.push_back(v);break;
    default:throw std::invalid_argument("Invalid GX geometry primitive");
    }
    return out;
}
GxGeometry GxGeometry::snapshot(const aurora::gx::GXRegisterState& state,GXPrimitive primitive,GXVtxFmt format,
    uint16_t count,std::span<const uint8_t> records,GxArrayResolver resolve,void* context) {
    return snapshot(GxVertexLayout::build(state,format),state,primitive,count,records,resolve,context);
}
GxGeometry GxGeometry::snapshot(const GxVertexLayout& layout,const aurora::gx::GXRegisterState& state,GXPrimitive primitive,
    uint16_t count,std::span<const uint8_t> records,GxArrayResolver resolve,void* context) {
    // Reject an invalid primitive before any array is resolved, as before.
    if(primitive!=GX_QUADS&&primitive!=GX_TRIANGLES&&primitive!=GX_TRIANGLESTRIP&&primitive!=GX_TRIANGLEFAN&&
       primitive!=GX_LINES&&primitive!=GX_LINESTRIP&&primitive!=GX_POINTS)
        throw std::invalid_argument("Invalid GX geometry primitive");
    auto out=snapshot_records(layout,state,count,records,resolve,context);
    out.primitive_=primitive;out.indices_=build_indices(primitive,count);
    return out;
}
GxGeometry GxGeometry::snapshot_segments(const GxVertexLayout& layout,const aurora::gx::GXRegisterState& state,
    std::span<const GxDrawSegment> segments,std::span<const uint8_t> records,GxArrayResolver resolve,void* context) {
    uint64_t total=0;size_t indexCount=0;
    for(const auto& segment:segments){
        const uint32_t n=segment.count;
        switch(segment.primitive){
        case GX_QUADS:indexCount+=(n/4)*6;break;
        case GX_TRIANGLES:indexCount+=(n/3)*3;break;
        case GX_TRIANGLESTRIP:case GX_TRIANGLEFAN:indexCount+=n>2?(size_t(n)-2)*3:0;break;
        default:throw std::invalid_argument("Only triangle GX primitives can share a draw");
        }
        total+=n;
    }
    require(total<=std::numeric_limits<uint32_t>::max(),"Merged GX draw exceeds 32-bit vertex indices");
    auto out=snapshot_records(layout,state,uint32_t(total),records,resolve,context);
    // Written in place: no per-command temporary vector (one allocation per
    // command dominated this function on PS5).
    out.indices_.resize(indexCount);
    uint32_t* cursor=out.indices_.data();
    uint32_t base=0;
    for(const auto& segment:segments){
        cursor=write_triangle_indices(cursor,segment.primitive,segment.count,base);
        base+=segment.count;
    }
    return out;
}
GxGeometry GxGeometry::snapshot_records(const GxVertexLayout& layout,const aurora::gx::GXRegisterState& state,
    uint32_t count,std::span<const uint8_t> records,GxArrayResolver resolve,void* context) {
    GxGeometry out;out.layout_=layout;
    out.count_=count;out.currentPnMtx_=state.currentPnMtx;
    require(records.size()==size_t(count)*out.layout_.stride,"GX vertex stream size mismatch");
    require(count==0 || state.vtxDesc[GX_VA_POS]!=GX_NONE,"GX draw has no position attribute");
    out.records_.assign(records.begin(),records.end());
    // All raw vertex values and indices follow Aurora's big-endian vbuf ABI,
    // independently of the endianness used for the FIFO command count.
    for(unsigned attr=GX_VA_POS;attr<GxGeometryAttributeCount && count;++attr) {
        const auto& d=out.layout_.attributes[attr];
        if(d.mode!=GX_INDEX8 && d.mode!=GX_INDEX16)continue;
        const auto& array=state.arrays[attr];
        require(resolve && array.data,"Missing GX vertex array source");
        unsigned indexWidth=d.mode==GX_INDEX16?2:1;
        uint32_t lo=std::numeric_limits<uint32_t>::max(),hi=0;
        // Every index lies inside the records validated against count*stride above.
        require(d.offset+size_t(d.indexCount)*indexWidth<=out.layout_.stride,"GX index outside vertex record");
        // One loop per index width and group count keeps the per-vertex body
        // free of branches the compiler cannot hoist.
        const uint8_t* record=out.records_.data();
        const uint32_t stride=out.layout_.stride,offset=d.offset;
        if(indexWidth==2&&d.indexCount==1){
            for(uint32_t v=0;v<count;++v,record+=stride){
                const uint32_t index=(uint32_t(record[offset])<<8)|record[offset+1];
                lo=std::min(lo,index);hi=std::max(hi,index);
            }
        }else if(indexWidth==1&&d.indexCount==1){
            for(uint32_t v=0;v<count;++v,record+=stride){
                const uint32_t index=record[offset];
                lo=std::min(lo,index);hi=std::max(hi,index);
            }
        }else{
            for(uint32_t v=0;v<count;++v,record+=stride)for(unsigned group=0;group<d.indexCount;++group) {
                const uint8_t* at=record+offset+group*indexWidth;
                const uint32_t index=indexWidth==2?(uint32_t(at[0])<<8)|at[1]:at[0];
                lo=std::min(lo,index);hi=std::max(hi,index);
            }
        }
        uint32_t elementBytes=d.componentBytes*(d.indexCount==3?3:d.components);
        uint64_t begin=uint64_t(lo)*d.arrayStride,end=uint64_t(hi)*d.arrayStride+elementBytes;
        require(end<=array.size,"GX index addresses bytes outside declared array");
        auto bytes=resolve(context,GXAttr(attr),array,uint32_t(begin),uint32_t(end-begin));
        require(bytes.size()>=end-begin,"Truncated resolved GX array");
        auto& snapshot=out.arrays_[attr];snapshot.sourceOffset=uint32_t(begin);
        snapshot.bytes.assign(bytes.begin(),bytes.begin()+size_t(end-begin));
    }
    return out;
}
static float component(std::span<const uint8_t> bytes,size_t at,const GxAttributeLayout& d,bool little) {
    uint32_t v=load(bytes,at,d.componentBytes,little);
    if(d.type==GX_F32)return std::bit_cast<float>(v);
    int32_t number=d.type==GX_S8?int32_t(v^128)-128:d.type==GX_S16?int32_t(v^32768)-32768:int32_t(v);
    return float(number)/float(uint32_t{1}<<d.fraction);
}
static std::array<float,4> color_value(std::span<const uint8_t> bytes,size_t at,const GxAttributeLayout& d,bool little) {
    if(d.type==GX_RGB8 || d.type==GX_RGBX8 || d.type==GX_RGBA8) {
        return {load(bytes,at,1,true)/255.0f,load(bytes,at+1,1,true)/255.0f,
            load(bytes,at+2,1,true)/255.0f,d.type==GX_RGBA8?load(bytes,at+3,1,true)/255.0f:1.0f};
    }
    uint32_t v=load(bytes,at,d.componentBytes,little);
    if(d.type==GX_RGB565)return {((v>>11)&31)/31.0f,((v>>5)&63)/63.0f,(v&31)/31.0f,1.0f};
    if(d.type==GX_RGBA4)return {((v>>12)&15)/15.0f,((v>>8)&15)/15.0f,((v>>4)&15)/15.0f,(v&15)/15.0f};
    return {((v>>18)&63)/63.0f,((v>>12)&63)/63.0f,((v>>6)&63)/63.0f,(v&63)/63.0f};
}
GxDecodedVertex decode_gx_vertex(const GxGeometry& geometry,uint32_t vertex) {
    require(vertex<geometry.vertex_count(),"GX vertex index outside packet");
    GxDecodedVertex out;out.positionMatrix=geometry.current_position_matrix();
    const auto& layout=geometry.layout();
    for(unsigned attr=0;attr<GxGeometryAttributeCount;++attr) {
        const auto& d=layout.attributes[attr];if(d.mode==GX_NONE)continue;
        size_t at=size_t(vertex)*layout.stride+d.offset;
        auto data=std::span(geometry.records());bool little=false;
        if(attr<GX_VA_POS) {
            auto value=load(data,at,1,false);
            if(attr==GX_VA_PNMTXIDX)out.positionMatrix=value/3;
            else out.textureMatrices[attr-GX_VA_TEX0MTXIDX]=value;
            continue;
        }
        const unsigned groups=attr==GX_VA_NRM && d.components==9?3:1;
        for(unsigned group=0;group<groups;++group) {
            auto bytes=data;size_t address=at+group*3*d.componentBytes;
            if(d.mode==GX_INDEX8 || d.mode==GX_INDEX16) {
                unsigned width=d.mode==GX_INDEX16?2:1;
                auto index=load(data,at+(d.indexCount==3?group*width:0),width,false);
                const auto& array=geometry.arrays()[attr];bytes=array.bytes;little=d.arrayLittleEndian;
                address=size_t(index)*d.arrayStride-array.sourceOffset+(d.indexCount==3?0:group*3*d.componentBytes);
            }
            if(color(attr)){out.colors[attr-GX_VA_CLR0]=color_value(bytes,address,d,little);continue;}
            unsigned count=attr==GX_VA_NRM?3:d.components;
            for(unsigned c=0;c<count;++c) {
                float value=component(bytes,address+c*d.componentBytes,d,little);
                if(attr==GX_VA_POS)out.position[c]=value;
                else if(attr==GX_VA_NRM)(group==0?out.normal:group==1?out.binormal:out.tangent)[c]=value;
                else out.uv[attr-GX_VA_TEX0][c]=value;
            }
        }
        if(attr==GX_VA_NRM && groups==1)out.binormal=out.tangent=out.normal;
    }
    return out;
}
}
