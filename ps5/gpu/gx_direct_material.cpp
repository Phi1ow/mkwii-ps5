// SPDX-License-Identifier: GPL-3.0-only
#include <cstdio>
#include "gx_direct_material.h"
#include "gx_fog_state.h"
#include "gx_sampler.h"
#include "gx/register_state.hpp"
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <type_traits>

namespace mkw::agc {
static_assert(sizeof(GxSampleBinding)==64 && sizeof(GxDirectUniforms)==2288);
static_assert(offsetof(GxDirectUniforms,textures)==1360 && offsetof(GxDirectUniforms,coordinateScales)==1872);
static_assert(offsetof(GxDirectUniforms,indirectStages)==2000 && offsetof(GxDirectUniforms,indirectMatrices)==2064);
static_assert(offsetof(GxDirectUniforms,fog)==MKW_GX_FOG_OFFSET);
static_assert(std::is_trivially_copyable_v<GxDirectUniforms>);
GxDirectMaterial GxDirectMaterial::build(const aurora::gx::GXRegisterState& state,GxTextureCache& cache,
    GxSourceResolver source,void* context,uint16_t configuredAnisotropy,bool viewportLodBias,GxCopyTextureCache* copies) {
    return build(mkw::gpu::build_tev_program(state),state,cache,source,context,configuredAnisotropy,viewportLodBias,copies);
}
GxDirectMaterial GxDirectMaterial::build(const MkwTevProgram& tevProgram,const aurora::gx::GXRegisterState& state,GxTextureCache& cache,
    GxSourceResolver source,void* context,uint16_t configuredAnisotropy,bool viewportLodBias,GxCopyTextureCache* copies) {
    GxDirectMaterial result;
    result.uniforms_.program=tevProgram;
    result.uniforms_.fog=snapshot_gx_fog(state);
    const auto& program=result.uniforms_.program;
    bool usesMatrices=false;
    for(unsigned n=0;n<program.stage_count;++n) {
        const auto& stage=program.stages[n];
        if(stage.indirect_stage!=~0u) {
            // The indirect stage samples its own map/coordinate before this stage;
            // bind that map like a direct one (GXSetIndTexOrder/GXSetIndTexCoordScale).
            const auto& ind=state.indStages[stage.indirect_stage];
            if(unsigned(ind.texMapId)>=8 || unsigned(ind.scaleS)>8 || unsigned(ind.scaleT)>8) {
                char message[128];
                std::snprintf(message,sizeof message,"Invalid GX indirect stage binding (map %u, scale %u/%u)",
                    unsigned(ind.texMapId),unsigned(ind.scaleS),unsigned(ind.scaleT));
                throw std::invalid_argument(message);
            }
            // Coordinate as Aurora's tev_effective_texcoord: an index outside the
            // generated set reads coordinate 0, and without texgens the coordinate
            // is fixed at zero (8 selects that constant in the shader).
            const unsigned coordinate=state.numTexGens==0?8u:unsigned(ind.texCoordId)<state.numTexGens?unsigned(ind.texCoordId):0u;
            result.mask_|=1u<<unsigned(ind.texMapId);
            result.uniforms_.indirectStages[stage.indirect_stage]={unsigned(ind.texMapId),coordinate,unsigned(ind.scaleS),unsigned(ind.scaleT)};
            if(stage.indirect_matrix!=0)usesMatrices=true;
        }
        if(stage.indirect_matrix>11 || stage.indirect_matrix==4 || stage.indirect_matrix==8 || (stage.indirect_wrap&255)>6 || (stage.indirect_wrap>>8)>6)
            throw std::invalid_argument("Invalid GX indirect matrix/wrap selection");
        if(stage.sample_enabled) {
            if(stage.tex_map>=8 || stage.tex_coord>=state.numTexGens)
                throw std::invalid_argument("Invalid direct TEV texture dependency");
            result.mask_|=1u<<stage.tex_map;
        }
    }
    if(usesMatrices) for(unsigned m=0;m<3;++m) {
        const auto& mtx=state.indTexMtxs[m];
        const auto mantissa=[](float v){if(!std::isfinite(v))throw std::invalid_argument("Nonfinite GX indirect matrix");return int32_t(std::lround(v*1024.0f));};
        result.uniforms_.indirectMatrices[2*m]={mantissa(mtx.mtx.m0.x),mantissa(mtx.mtx.m0.y),mantissa(mtx.mtx.m1.x),mantissa(mtx.mtx.m1.y)};
        result.uniforms_.indirectMatrices[2*m+1]={mantissa(mtx.mtx.m2.x),mantissa(mtx.mtx.m2.y),-int32_t(mtx.scaleExp),0};
    }
    for(unsigned n=0;n<state.numTexGens;++n) {
        const auto& scale=state.texCoordScales[n];
        result.uniforms_.coordinateScales[n]={float(scale.scaleS)+1,float(scale.scaleT)+1,
            scale.biasS?1.0f:0.0f,scale.biasT?1.0f:0.0f};
    }
    if(!copies)copies=&gx_copy_texture_cache();
    for(unsigned n=0;n<8;++n) if(result.mask_&(1u<<n)) {
        const auto& texture=state.loadedTextures[n];
        if(auto copy=copies->resolve(texture)) {
            auto& binding=result.uniforms_.textures[n];
            binding.texture=copy->target->texture_descriptor();
            binding.sampler=gx_sampler(texture,false,configuredAnisotropy);
            // Normalized sampling still uses the Wii dimensions, even when
            // the rendered texture has a larger physical resolution.
            binding.parameters={1.f/(128.f*float(copy->width)),1.f/(128.f*float(copy->height)),texture.lod_bias(),0};
            result.colorCopies_[n]=std::move(copy);
            continue;
        }
        if(!source)throw std::invalid_argument("Missing GX texture source resolver");
        const GXTlutObj_* palette=nullptr;
        const auto format=texture.format();
        if(format==GX_TF_C4 || format==GX_TF_C8 || format==GX_TF_C14X2) {
            if(unsigned(texture.tlut)>=state.loadedTluts.size())
                throw std::invalid_argument("GX palette index outside loaded table");
            palette=&state.loadedTluts[texture.tlut];
        }
        // Validate sampler before allocating this texture. Decoding determines
        // whether authored mipmaps need Aurora's linear mip filtering policy.
        gx_sampler(texture,false,configuredAnisotropy);
        const auto bytes=source(context,texture,palette);
        auto uploaded=cache.resolve(texture,bytes.texture,palette,bytes.palette);
        float bias=texture.lod_bias();
        if(viewportLodBias && uploaded->hasArbitraryMips) {
            const auto& r=state.renderViewport;const auto& l=state.logicalViewport;
            if(!std::isfinite(r.width) || !std::isfinite(r.height) ||
               !std::isfinite(l.width) || !std::isfinite(l.height) || r.width<=0 || r.height<=0)
                throw std::invalid_argument("Invalid viewport for GX texture LOD bias");
            bias+=std::log2(std::min(r.width/std::max(l.width,1.0f),r.height/std::max(l.height,1.0f)));
            if(!std::isfinite(bias)) throw std::invalid_argument("Unrepresentable GX viewport LOD bias");
        }
        auto& binding=result.uniforms_.textures[n];
        binding.texture=uploaded->gpu.descriptor();
        binding.sampler=gx_sampler(texture,uploaded->hasArbitraryMips,configuredAnisotropy);
        binding.parameters={1.0f/(128.0f*float(texture.width())),
                            1.0f/(128.0f*float(texture.height())),bias,0};
        result.textures_[n]=std::move(uploaded);
    }
    return result;
}
}
