// SPDX-License-Identifier: GPL-3.0-only
// Container layout follows ps5link-sdk and SharpProspero ShaderBinary.Load.
#include "gpu_shader.h"
#include <stdexcept>
#include <string_view>
namespace mkw::agc {
ShaderBinaryParts parse_shader_binary(std::span<const uint8_t> bytes){
    auto bad=[](){throw std::invalid_argument("Invalid AGC shader container");};
    auto range=[&](uint64_t off,uint64_t size){if(off>bytes.size()||size>bytes.size()-off)bad();return bytes.subspan(size_t(off),size_t(size));};
    auto read=[&](uint64_t off,unsigned count){auto p=range(off,count);uint64_t value=0;for(unsigned i=0;i<count;++i)value|=uint64_t(p[i])<<(8*i);return value;};
    if(bytes.size()<64||read(0,4)!=0x464c457f||bytes[4]!=2||bytes[5]!=1||bytes[6]!=1)bad();
    const auto table=read(40,8),stride=read(58,2),count=read(60,2),namesIndex=read(62,2);
    if(!table||stride<64||!count||namesIndex>=count)bad();
    (void)range(table,stride*count);
    auto section=[&](uint64_t index){const auto pos=table+index*stride;return range(read(pos+24,8),read(pos+32,8));};
    if(read(table+namesIndex*stride+4,4)!=3)bad();
    const auto names=section(namesIndex);ShaderBinaryParts result{};
    for(uint64_t i=0;i<count;++i){
        const auto pos=table+i*stride,nameOffset=read(pos,4);
        if(nameOffset>=names.size())bad();
        auto rest=names.subspan(size_t(nameOffset));size_t len=0;while(len<rest.size()&&rest[len])++len;
        if(len==rest.size())bad();
        std::string_view name(reinterpret_cast<const char*>(rest.data()),len);
        auto assign=[&](std::span<const uint8_t>& dest){if(!dest.empty()||read(pos+4,4)!=1)bad();dest=section(i);if(dest.empty())bad();};
        if(name==".shader_header")assign(result.header);else if(name==".shader_text")assign(result.code);
    }
    if(result.header.size()<96||result.code.empty())bad();
    if(result.header[0]!='1'||result.header[1]!='2'||result.header[2]!='3'||result.header[3]!='4')bad();
    const auto hb=result.header.data(),he=hb+result.header.size(),cb=result.code.data(),ce=cb+result.code.size();
    if(hb<ce&&cb<he)bad();
    return result;
}
}
