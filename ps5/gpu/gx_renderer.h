// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "agc_gx_draw.h"
#include "gx_draw_backend.h"
namespace mkw::agc {
// Receiver for actual WiiCompiled FIFO geometry. Game-owned resolvers must
// validate guest RAM or registered host allocations, including deferred EFB
// reads. This class never accepts an unvalidated arbitrary host pointer.
class GxRenderer {
public:
    GxRenderer(void* vertexShader,void* pixelShader,std::shared_ptr<GpuColorTarget>,
        std::shared_ptr<GpuDepthTarget>,GxArrayResolver,GxSourceResolver,void* sources,
        uint16_t anisotropy=1);
    GxDrawSink sink() noexcept;
    uint64_t completed_draws() const noexcept{return completed_;}
    uint64_t texture_uploads() const noexcept{return textures_.uploads();}
    uint64_t texture_digests() const noexcept{return textures_.digests();}
    uint64_t texture_digest_skips() const noexcept{return textures_.digest_skips();}
    void finish() const;
    // Submits pending draws without waiting for the GPU (see AgcGxDraw::submit).
    void submit_draws() const;
    void clear_texture_cache();
private:
    AgcGxDraw draw_;
    std::shared_ptr<GpuColorTarget> color_;
    std::shared_ptr<GpuDepthTarget> depth_;
    GxTextureCache textures_;
    GxArrayResolver arrays_;GxSourceResolver source_;void* context_;uint16_t anisotropy_;
    uint64_t completed_=0;
    void submit(GxGeometry&&,const aurora::gx::GXRegisterState&);
};
}
