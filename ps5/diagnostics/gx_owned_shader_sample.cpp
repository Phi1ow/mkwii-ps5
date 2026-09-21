// SPDX-License-Identifier: GPL-3.0-only
#include "gpu_shader.h"
#include <array>
#include <cstdio>
#include <exception>
extern "C" void mkw_diagnostic_log(const char*);
extern "C" int mkw_test_gx_frame(void*,void*,void*,void*);
extern "C" int mkw_test_owned_shader_frames(const unsigned char* vs,unsigned long vsSize,
    const unsigned char* ps,unsigned long psSize,const unsigned char* copyVs,unsigned long copyVsSize,
    const unsigned char* copyPs,unsigned long copyPsSize){
    std::array<std::unique_ptr<mkw::agc::GpuShader>,4> shaders;
    bool submitted=false;
    try{
        std::array<std::span<const uint8_t>,4> input{{{vs,vsSize},{ps,psSize},{copyVs,copyVsSize},{copyPs,copyPsSize}}};
        for(unsigned i=0;i<4;++i)shaders[i]=std::make_unique<mkw::agc::GpuShader>(input[i]);
        mkw_diagnostic_log("[mkw-shader-owner] four containers parsed, GPU headers/code owned and prepared\n");
        submitted=true;
        const int result=mkw_test_gx_frame(shaders[0]->handle(),shaders[1]->handle(),shaders[2]->handle(),shaders[3]->handle());
        // A failed GPU wait can leave work in flight. Keep shader storage as
        // long as those failed renderer resources (until process teardown).
        if(result){for(auto& shader:shaders)(void)shader.release();return result;}
        for(auto& shader:shaders)shader.reset();
        mkw_diagnostic_log("[mkw-shader-owner] PASS six frame cycles from four owned shader binaries; released after renderer close\n");return 0;
    }catch(const std::exception& e){
        if(submitted)for(auto& shader:shaders)(void)shader.release();
        mkw_diagnostic_log("[mkw-shader-owner] FAIL ");mkw_diagnostic_log(e.what());mkw_diagnostic_log("\n");return 1;
    }
}
