#include "frontend.hpp"
#include "__gx.h"

aurora::Module Log("aurora::gx");

extern "C" {
void __GXFlushTextureState() {
  GX_WRITE_RAS_REG(__gx->bpMask);
  __gx->bpSent = 1;
}
}
