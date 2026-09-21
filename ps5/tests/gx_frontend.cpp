// Test-only renderer boundary observers. No renderer substitutes are linked
// into mkw_ps5_gx_frontend or the game link audit. This verifies the producer
// byte stream, not rendering, GXInit, or command decoding.
#include "dolphin/gx/frontend.hpp"
#include "dolphin/gx/__gx.h"
#include "gx/command_processor.hpp"
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <span>
#include <stdexcept>
#include <vector>

namespace fifo = aurora::gx::fifo;
static __GXData_struct shadow{};
__GXData_struct* __gx = &shadow;
static unsigned resets, waits, submissions, checks;
static std::vector<uint8_t> submitted;
namespace aurora {
AuroraConfig g_config{};
std::chrono::nanoseconds wait_for_frame_worker_sealed() noexcept { ++waits; return {}; }
}
namespace aurora::gx::fifo {
void reset_cp_register_cache() { ++resets; }
void process(const uint8_t* data, uint32_t size, bool bigEndian) {
  if (!bigEndian || !waits) throw std::runtime_error("invalid drain ordering/endianness");
  ++submissions;
  submitted.assign(data, data + size);
}
}

static void require(bool ok, const char* label) {
  ++checks;
  if (!ok) throw std::runtime_error(label);
}
static void be32(std::vector<uint8_t>& data, uint32_t word) {
  data.push_back(word >> 24); data.push_back(word >> 16);
  data.push_back(word >> 8); data.push_back(word);
}
static void expect_bytes(std::span<const uint8_t> expected, const char* label) {
  require(fifo::get_buffer_size() == expected.size(), label);
  require(std::equal(expected.begin(), expected.end(), fifo::get_buffer_data()), label);
  fifo::clear_buffer();
}
static void expect_bp(std::initializer_list<uint32_t> words, const char* label) {
  std::vector<uint8_t> expected;
  for (auto word : words) { expected.push_back(0x61); be32(expected, word); }
  expect_bytes(expected, label);
}
static void expect_xf(uint32_t header, std::initializer_list<uint32_t> words, const char* label) {
  std::vector<uint8_t> expected{0x10}; be32(expected, header);
  for (auto word : words) be32(expected, word);
  expect_bytes(expected, label);
}
static void reset_shadow() {
  shadow = {};
  for (unsigned i = 0; i < 16; ++i) {
    shadow.tevc[i] = (0xc0u + i * 2) << 24;
    shadow.teva[i] = (0xc1u + i * 2) << 24;
  }
  for (unsigned i = 0; i < 8; ++i) {
    shadow.tref[i] = (0x28u + i) << 24;
    shadow.tevKsel[i] = (0xf6u + i) << 24;
  }
  shadow.cmode0 = 0x41000000; shadow.zmode = 0x40000000;
  shadow.suScis0 = 0x20000000; shadow.suScis1 = 0x21000000;
  shadow.iref = 0x27000000;
  shadow.IndTexScale0 = 0x25000000; shadow.IndTexScale1 = 0x26000000;
}
static void tev() {
  GXSetTevOp(GX_TEVSTAGE0, GX_MODULATE);
  expect_bp({0xc000f8af, 0xc100f2f0, 0xc008f8af, 0xc108f2f0}, "stage 0 modulate");
  GXSetTevOp(GX_TEVSTAGE1, GX_MODULATE);
  expect_bp({0xc200f80f, 0xc300f070, 0xc208f80f, 0xc308f070}, "stage 1 previous color");
  GXSetTevColorS10(GX_TEVREG1, {-1024, -1, 1023, 1});
  expect_bp({0xe4001400, 0xe57ff3ff}, "signed TEV register endpoints");
  GXSetTevKColor(GX_KCOLOR3, {0x12, 0x34, 0x56, 0x78});
  expect_bp({0xe6878012, 0xe7834056}, "konst color bank");
  GXSetAlphaCompare(GX_LEQUAL, 0x12, GX_AOP_XOR, GX_GEQUAL, 0xab);
  expect_bp({0xf3b3ab12}, "alpha comparisons");
  GXSetZTexture(GX_ZT_REPLACE, GX_TF_Z16, 0x12345678);
  expect_bp({0xf4345678, 0xf5000009}, "Z texture format and bias");
  GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD2, GX_TEXMAP3, GX_COLOR0A0);
  GXSetTevOrder(GX_TEVSTAGE1, GX_TEXCOORD_NULL, GX_TEXMAP_NULL, GX_COLOR_NULL);
  expect_bp({0x28000053, 0x28380053}, "paired TEV orders preserve neighbour");
  require(shadow.texmapValid == 1 && (shadow.dirtyState & 1), "TEV texture validity");
  GXSetTevColorOp(GX_TEVSTAGE0, GX_TEV_COMP_BGR24_EQ, GX_TB_ZERO, GX_CS_SCALE_1, false, GX_TEVREG2);
  expect_bp({0xc0e7f8af}, "TEV comparison operation and output register");
}
static void raster() {
  GXSetZMode(true, GX_LEQUAL, true);
  GXSetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_COPY);
  GXSetColorUpdate(true); GXSetAlphaUpdate(true);
  expect_bp({0x40000017, 0x410034a1, 0x410034a9, 0x410034b9}, "depth and blend state");
  GXSetScissor(0, 0, 640, 480);
  expect_bp({0x20156156, 0x213d5335}, "640x480 inclusive scissor with GX bias");
  GXSetClipMode(GX_CLIP_DISABLE);
  expect_xf(0x1005, {1}, "XF clip register");
  GXSetCoPlanar(true);
  expect_bp({0xfe080000, 0x00080000}, "one-shot BP update mask");
  GXSetCullMode(GX_CULL_BACK);
  require(shadow.genMode == 0x84000 && (shadow.dirtyState & 4), "GX winding conversion");
  GXSetIndTexOrder(GX_INDTEXSTAGE2, GX_TEXCOORD3, GX_TEXMAP2);
  GXSetIndTexCoordScale(GX_INDTEXSTAGE3, GX_ITS_8, GX_ITS_16);
  expect_bp({0x2701a000, 0x26004300}, "indirect texture routing and scale");
}
static void lighting() {
  GXLightObj light{};
  GXInitLightColor(&light, {0x12, 0x34, 0x56, 0x78});
  GXInitLightAttn(&light, 1, 0, 0, 1, 0, 0);
  GXInitLightPos(&light, 1, -2, 3);
  GXInitLightDir(&light, 0, 0, -1);
  GXLoadLightObjImm(&light, GX_LIGHT2);
  expect_xf(0x000f0620,
      {0, 0, 0, 0x12345678, 0x3f800000, 0, 0, 0x3f800000, 0, 0,
       0x3f800000, 0xc0000000, 0x40400000, 0x80000000, 0x80000000, 0x3f800000},
      "light XF bulk write and IEEE floats");
}
static void transport() {
  // Actual FIFO grow path, preserving more than its initial 64 KiB.
  std::vector<uint8_t> bytes(70003);
  for (size_t i = 0; i < bytes.size(); ++i) bytes[i] = static_cast<uint8_t>(i * 37 + 19);
  fifo::write_data(bytes.data(), static_cast<uint32_t>(bytes.size()));
  fifo::drain();
  require(submitted == bytes && submissions == 1 && waits == 1, "grow/drain byte preservation");
  require(fifo::get_buffer_size() == 0, "drain consumes buffer");
  fifo::drain();
  require(submissions == 1 && waits == 2, "empty drain synchronization");
  std::array<uint8_t, 96> dl; dl.fill(0xcd);
  fifo::begin_display_list(dl.data(), 64);
  GXSetTevKColor(GX_KCOLOR3, {0x12, 0x34, 0x56, 0x78});
  require(fifo::get_buffer_size() == 0, "display list redirects producer writes");
  require(fifo::end_display_list() == 32 && !fifo::in_display_list(), "display list alignment");
  const std::array<uint8_t, 10> prefix{0x61,0xe6,0x87,0x80,0x12,0x61,0xe7,0x83,0x40,0x56};
  require(std::equal(prefix.begin(), prefix.end(), dl.begin()), "display list GX bytes");
  require(std::all_of(dl.begin()+10, dl.begin()+32, [](auto b){return b==0;}), "display list padding");
  require(std::all_of(dl.begin()+32, dl.end(), [](auto b){return b==0xcd;}), "display list guard");
  fifo::write_u16(0x1234); fifo::write_u32(0x89abcdef);
  fifo::write_u64(0x0123456789abcdef); fifo::write_f32(-2.0f);
  const std::array<uint8_t,18> scalars{0x12,0x34,0x89,0xab,0xcd,0xef,0x01,0x23,0x45,0x67,0x89,0xab,0xcd,0xef,0xc0,0,0,0};
  expect_bytes(scalars, "mixed-width FIFO endianness");
}
int main() {
  try {
    fifo::init(); reset_shadow();
    require(resets == 1, "FIFO resets command processor cache");
    tev(); raster(); lighting(); transport();
    std::free(fifo::detail::sBufferData); fifo::detail::sBufferData = nullptr;
    std::printf("PASS GX producer: %u checks, TEV/BP/XF, light floats, FIFO growth and display capture; no renderer simulated\n", checks);
    return 0;
  } catch (const std::exception& error) {
    std::fprintf(stderr, "FAIL GX producer: %s\n", error.what()); return 1;
  }
}
