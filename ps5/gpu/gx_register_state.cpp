// Register storage for the native backend. Default texture-coordinate sources
// match WiiCompiled's make_default_gx_state. GPU resource ownership is separate.
#include "gx/register_state.hpp"
namespace aurora::gx {
namespace {
GXRegisterState state = [] {
  GXRegisterState value{};
  for (size_t i = 0; i < value.tcgs.size(); ++i)
    value.tcgs[i].src = static_cast<GXTexGenSrc>(GX_TG_TEX0 + i);
  return value;
}();
}
GXRegisterState& register_state() noexcept { return state; }
} // namespace aurora::gx

namespace aurora {
std::recursive_mutex& renderer_gpu_mutex() noexcept {
  static std::recursive_mutex mutex;
  return mutex;
}
} // namespace aurora
