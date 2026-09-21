// SPDX-License-Identifier: GPL-3.0-only
#include "gx_viewport.h"
#include "gx/register_backend.hpp"
#include <cstring>
#include <exception>
extern "C" void mkw_diagnostic_log(const char*);
extern "C" int mkw_viewport_frame(unsigned frame,void* registers){
    try {
        namespace gx=aurora::gx;
        // Same callback path consumed by the production FIFO decoder.
        gx::register_state().viewportPolicy=AURORA_VIEWPORT_FIT;
        mkw::agc::configure_gx_render_extent({640,480,1920,1080});
        gx::set_logical_viewport({0,0,640,480,0,1});
        constexpr aurora::gfx::ClipRect scissors[]={{0,0,320,480},{320,0,320,480},{0,0,640,240},{0,240,640,240}};
        gx::set_logical_scissor(scissors[frame%4]);
        const auto& s=gx::register_state();
        auto block=mkw::agc::snapshot_gx_viewport({s.renderViewport,s.renderScissor});
        std::memcpy(registers,block.data(),sizeof(block));return 0;
    }catch(const std::exception& e){mkw_diagnostic_log("[mkw-viewport] FAIL ");mkw_diagnostic_log(e.what());mkw_diagnostic_log("\n");return 1;}
}
