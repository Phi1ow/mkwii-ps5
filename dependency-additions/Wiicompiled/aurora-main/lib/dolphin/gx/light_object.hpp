#pragma once
#include <dolphin/gx.h>

struct GXLightObj_ {
  GXColor color;
  float a0 = 1.f;
  float a1 = 0.f;
  float a2 = 0.f;
  float k0 = 1.f;
  float k1 = 0.f;
  float k2 = 0.f;
  float px = 0.f;
  float py = 0.f;
  float pz = 0.f;
  float nx = 0.f;
  float ny = 0.f;
  float nz = 0.f;
};
static_assert(sizeof(GXLightObj_) <= sizeof(GXLightObj), "GXLightObj too small!");

#if GX_IS_WII
constexpr float GX_LARGE_NUMBER = -1.0e+18f;
#else
constexpr float GX_LARGE_NUMBER = -1048576.0f;
#endif

