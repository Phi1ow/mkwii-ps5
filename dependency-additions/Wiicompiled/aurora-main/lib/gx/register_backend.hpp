#pragma once
#include "register_state.hpp"
#include <string>

// Callbacks required from a real renderer. There are no default/no-op
// implementations: omitting a backend remains a link failure.
namespace aurora::gx {
void set_render_viewport(const gfx::Viewport& viewport) noexcept;
void set_render_scissor(const gfx::ClipRect& scissor) noexcept;
void evict_texture_object(u32 id) noexcept;
void evict_tlut_object(u32 id) noexcept;
void evict_copy_texture(const void* destination) noexcept;
void invalidate_static_texture_cache() noexcept;
namespace fifo {
bool handle_draw(u8 command, const u8* data, u32& position, u32 size, bool bigEndian);
}
}
namespace aurora::gfx {
void push_debug_group(std::string label);
void insert_debug_marker(std::string label);
}
