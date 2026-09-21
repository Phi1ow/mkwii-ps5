#pragma once
// Immutable draw-time TEV snapshot. Sampling/indirect coordinates are supplied
// by the renderer; no unsupported renderer feature is silently disabled here.
#include "tev_stage.h"
namespace aurora::gx { struct GXRegisterState; }
namespace mkw::gpu {
MkwTevProgram build_tev_program(const aurora::gx::GXRegisterState& state);
// Bitmask of texture coordinates the vertex shader must emit: sampled stage
// coordinates, the persistent indirect coordinate state, and every active
// indirect stage coordinate (with GX's coordinate-0 fallback).
unsigned tev_program_used_texcoords(const aurora::gx::GXRegisterState& state);
}
