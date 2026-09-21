// SPDX-License-Identifier: GPL-3.0-only
// Unused presentation/draw boundaries fail if reached. Memory and texture
// descriptor sizing use the production implementations in this test.
#include "dolphin/gx/frontend.hpp"
#include "dolphin/gx/__gx.h"
#include "gx/command_processor.hpp"
#include "gx/register_backend.hpp"
#include <aurora/gfx.h>
#include <exception>
#include <stdexcept>
static __GXData_struct shadow{};
__GXData_struct* __gx=&shadow;
namespace aurora {
AuroraConfig g_config{};
std::chrono::nanoseconds wait_for_frame_worker_sealed() noexcept { return {}; }
}
namespace aurora::gx {
void set_logical_viewport(const gfx::Viewport&) noexcept { std::terminate(); }
void set_logical_scissor(const gfx::ClipRect&) noexcept { std::terminate(); }
void set_render_viewport(const gfx::Viewport&) noexcept { std::terminate(); }
void set_render_scissor(const gfx::ClipRect&) noexcept { std::terminate(); }
namespace fifo { bool handle_draw(u8,const u8*,u32&,u32,bool) { throw std::runtime_error("unexpected draw"); } }
}
namespace aurora::gfx {
void push_debug_group(std::string) { throw std::runtime_error("unexpected debug group"); }
void insert_debug_marker(std::string) { throw std::runtime_error("unexpected debug marker"); }
}
extern "C" void __GXSetDirtyState(){throw std::runtime_error("Unexpected dirty flush");}
extern "C" void aurora_pop_debug_group() { std::terminate(); }
