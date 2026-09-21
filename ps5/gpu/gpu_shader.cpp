// SPDX-License-Identifier: GPL-3.0-only
#include "gpu_shader.h"
#include <cstring>
#include <stdexcept>
extern "C" int sceAgcCreateShader(void**,void*,void*);
namespace mkw::agc {
GpuShader::GpuShader(std::span<const uint8_t> binary){
    const auto parts=parse_shader_binary(binary);
    header_=std::make_unique<GpuBuffer>(parts.header.size());code_=std::make_unique<GpuBuffer>(parts.code.size());
    std::memcpy(header_->data(),parts.header.data(),parts.header.size());std::memcpy(code_->data(),parts.code.data(),parts.code.size());
    if(sceAgcCreateShader(&handle_,header_->data(),code_->data())<0||!handle_)throw std::runtime_error("AGC shader preparation failed");
    // Preparing a shader patches its header in place. Publish those writes too.
    header_->flush(parts.header.size());code_->flush(parts.code.size());
}
}
