// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "gpu_buffer.h"
#include <memory>
#include <span>
namespace mkw::agc {
struct ShaderBinaryParts {std::span<const uint8_t> header,code;};
// Bounded ELF64 parser for the two sections used by the supplied SDKs.
// Returned views borrow the container; GpuShader makes GPU-owned copies.
ShaderBinaryParts parse_shader_binary(std::span<const uint8_t>);
class GpuShader {
public:
    explicit GpuShader(std::span<const uint8_t>);
    GpuShader(const GpuShader&)=delete;
    GpuShader& operator=(const GpuShader&)=delete;
    // Caller must retire every GPU user before destroying this owner.
    void* handle() const noexcept{return handle_;}
private:
    std::unique_ptr<GpuBuffer> header_,code_;
    void* handle_=nullptr;
};
}
