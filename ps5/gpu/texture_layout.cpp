// SPDX-License-Identifier: GPL-3.0-only
// Derived from SharpProspero AgcSurfaceTiling.cs, AgcSurfaceTables.cs,
// AgcTiler.cs and AgcTilingTables.cs, Copyright (C) 2026 SvenGDK.
// The 2D RGBA8 single-sample specialization uses equation 2 verbatim.
#include "texture_layout.h"
#include <algorithm>
#include <cstring>
#include <stdexcept>
namespace mkw::agc {
namespace {
constexpr uint32_t tail[12][2]={{64,0},{0,64},{32,0},{0,32},{16,0},{8,16},{0,24},{0,16},{8,8},{8,0},{0,8},{0,0}};
constexpr uint32_t aligned(uint32_t n) { return (n+127)&~127u; }
constexpr uint32_t ceil_mip(uint32_t n,uint32_t m) { return std::max(1u,(n+(1u<<m)-1)>>m); }
uint32_t swizzle(uint32_t x,uint32_t y) {
    return ((y<<4)&0x70u)^((y<<5)&0xf00u)^((y<<9)&0x1000u)^((y<<8)&0x4000u)^
        ((x<<2)&0xcu)^((x<<5)&0x380u)^((x<<4)&0x400u)^((x<<6)&0x800u)^((x<<9)&0xa000u);
}
}
TextureLayout::TextureLayout(uint32_t width,uint32_t height,uint32_t mipCount)
:width_(width),height_(height),count_(mipCount) {
    if(!width || !height || width>16384 || height>16384 || !mipCount || mipCount>15)
        throw std::invalid_argument("Invalid AGC texture dimensions/mips");
    uint32_t maxMips=1;
    for(uint32_t d=std::max(width,height);d>1;d>>=1)++maxMips;
    if(mipCount>maxMips) throw std::invalid_argument("Too many AGC texture mips");
    uint32_t firstTail=mipCount;
    std::array<size_t,15> sizes{};
    for(uint32_t m=0;m<mipCount;++m) {
        auto& record=mips_[m];
        record.width=std::max(1u,width>>m);record.height=std::max(1u,height>>m);
        linearBytes_+=size_t(record.width)*record.height*4;
        uint32_t wc=ceil_mip(width,m),hc=ceil_mip(height,m);
        if(mipCount>1 && firstTail==mipCount && wc<=64 && hc<=128 && mipCount-m<=12)
            firstTail=m;
        if(m<firstTail) {
            record.paddedWidth=aligned(wc);
            sizes[m]=size_t(record.paddedWidth)*aligned(hc)*4;
            bytes_+=sizes[m];
        } else {
            record.paddedWidth=128;record.tailX=tail[m-firstTail][0];record.tailY=tail[m-firstTail][1];
        }
    }
    size_t offset=firstTail<mipCount?65536:0;
    bytes_+=offset;
    for(uint32_t m=firstTail;m>0;) { --m;mips_[m].offset=offset;offset+=sizes[m]; }
}
const TextureLayout::Mip& TextureLayout::mip(uint32_t level) const {
    if(level>=count_)throw std::out_of_range("AGC mip level");
    return mips_[level];
}
size_t TextureLayout::pixel_offset(uint32_t x,uint32_t y,uint32_t level) const {
    const auto& m=mip(level);
    if(x>=m.width || y>=m.height)throw std::out_of_range("AGC texel coordinate");
    size_t block=size_t(y>>7)*(m.paddedWidth>>7)+(x>>7);
    return m.offset+(block<<16)+swizzle(x+m.tailX,y+m.tailY);
}
void TextureLayout::tile(std::span<uint8_t> storage,std::span<const uint8_t> pixels) const {
    if(storage.size()<bytes_ || pixels.size()!=linearBytes_)
        throw std::invalid_argument("AGC texture byte size mismatch");
    auto dst=reinterpret_cast<uintptr_t>(storage.data()),src=reinterpret_cast<uintptr_t>(pixels.data());
    if(dst<=src ? src-dst<bytes_ : dst-src<linearBytes_)
        throw std::invalid_argument("AGC upload source and destination overlap");
    // Validate before modifying destination; padding must never contain old data.
    std::fill_n(storage.data(),bytes_,uint8_t{0});
    size_t source=0;
    for(uint32_t level=0;level<count_;++level) {
        const auto& m=mips_[level];
        for(uint32_t y=0;y<m.height;++y)for(uint32_t x=0;x<m.width;++x) {
            std::memcpy(storage.data()+pixel_offset(x,y,level),pixels.data()+source,4);
            source+=4;
        }
    }
}
std::array<uint32_t,8> TextureLayout::descriptor(uint64_t address) const {
    // Mode 27 needs a 64 KiB base. Descriptor byte-address capacity is 48 bits.
    if((address&65535) || address>=(uint64_t{1}<<48))throw std::invalid_argument("AGC texture base alignment/range");
    const uint32_t w=width_-1,h=height_-1;
    return {uint32_t(address>>8),uint32_t((address>>40)&255)|(56u<<20)|((w&3)<<30),
        (w>>2)|(h<<14),4u|(5u<<3)|(6u<<6)|(7u<<9)|((count_-1)<<16)|(27u<<20)|(9u<<28),
        0u,(count_-1)<<4,0u,0u};
}
std::array<uint32_t,8> TextureLayout::bgra_descriptor(uint64_t address) const {
    auto words=descriptor(address);
    // SharpProspero AgcTextureDescriptor.SetChannelOrder: B,G,R,A.
    words[3]=(words[3]&~0xfffu)|6u|(5u<<3)|(4u<<6)|(7u<<9);
    return words;
}
}
