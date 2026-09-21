#pragma once
// GX register producers do not own renderer resources. Keep their dependencies
// independent of WebGPU so the same FIFO commands can feed a native backend.
#include "../../internal.hpp"
#include "light_object.hpp"
#include <dolphin/gx.h>
#include <algorithm>
#include <cmath>
#include <cstring>

#ifndef M_PIF
#define M_PIF 3.14159265358979323846f
#endif
extern aurora::Module Log;
