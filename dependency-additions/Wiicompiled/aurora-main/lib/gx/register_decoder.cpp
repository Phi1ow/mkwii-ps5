#include "register_decoder.hpp"
#include "command_reader.hpp"
#include "gx_fmt.hpp"
#include <algorithm>
#include <optional>
#include <tracy/Tracy.hpp>

namespace aurora::gx::fifo {
static Module Log("aurora::gx::fifo");

static u32 bp_get(u32 reg, u32 size, u32 shift);

static GXPixelFmt decode_pixel_fmt(u32 peCtrl, u32 cmode1) {
  switch (bp_get(peCtrl, 3, 0)) {
  case 0:
    return GX_PF_RGB8_Z24;
  case 1:
    return GX_PF_RGBA6_Z24;
  case 2:
    return GX_PF_RGB565_Z16;
  case 3:
    return GX_PF_Z24;
  case 4:
    switch (bp_get(cmode1, 2, 9)) {
    case 0:
      return GX_PF_Y8;
    case 1:
      return GX_PF_U8;
    case 2:
      return GX_PF_V8;
    default:
      Log.warn("command_processor: unsupported cmode1 pixel subtype {}", bp_get(cmode1, 2, 9));
      return GX_PF_Y8;
    }
  case 5:
    return GX_PF_YUV420;
  default:
    Log.warn("command_processor: unsupported PE pixel format {}", bp_get(peCtrl, 3, 0));
    return GX_PF_RGB8_Z24;
  }
}

struct TexBpRegMapping {
  u8 texMapId;
  enum class Kind : uint8_t { Mode0, Mode1, Image0, Image1, Image2, Image3, Tlut } kind;
};

static std::optional<TexBpRegMapping> decode_tex_bp_reg(u32 regId) {
  constexpr std::array mode0Ids{0x80u, 0x81u, 0x82u, 0x83u, 0xA0u, 0xA1u, 0xA2u, 0xA3u};
  constexpr std::array mode1Ids{0x84u, 0x85u, 0x86u, 0x87u, 0xA4u, 0xA5u, 0xA6u, 0xA7u};
  constexpr std::array image0Ids{0x88u, 0x89u, 0x8Au, 0x8Bu, 0xA8u, 0xA9u, 0xAAu, 0xABu};
  constexpr std::array image1Ids{0x8Cu, 0x8Du, 0x8Eu, 0x8Fu, 0xACu, 0xADu, 0xAEu, 0xAFu};
  constexpr std::array image2Ids{0x90u, 0x91u, 0x92u, 0x93u, 0xB0u, 0xB1u, 0xB2u, 0xB3u};
  constexpr std::array image3Ids{0x94u, 0x95u, 0x96u, 0x97u, 0xB4u, 0xB5u, 0xB6u, 0xB7u};
  constexpr std::array tlutIds{0x98u, 0x99u, 0x9Au, 0x9Bu, 0xB8u, 0xB9u, 0xBAu, 0xBBu};

  for (u8 i = 0; i < MaxTextures; ++i) {
    if (regId == mode0Ids[i]) {
      return TexBpRegMapping{.texMapId = i, .kind = TexBpRegMapping::Kind::Mode0};
    }
    if (regId == mode1Ids[i]) {
      return TexBpRegMapping{.texMapId = i, .kind = TexBpRegMapping::Kind::Mode1};
    }
    if (regId == image0Ids[i]) {
      return TexBpRegMapping{.texMapId = i, .kind = TexBpRegMapping::Kind::Image0};
    }
    if (regId == image1Ids[i]) {
      return TexBpRegMapping{.texMapId = i, .kind = TexBpRegMapping::Kind::Image1};
    }
    if (regId == image2Ids[i]) {
      return TexBpRegMapping{.texMapId = i, .kind = TexBpRegMapping::Kind::Image2};
    }
    if (regId == image3Ids[i]) {
      return TexBpRegMapping{.texMapId = i, .kind = TexBpRegMapping::Kind::Image3};
    }
    if (regId == tlutIds[i]) {
      return TexBpRegMapping{.texMapId = i, .kind = TexBpRegMapping::Kind::Tlut};
    }
  }
  return std::nullopt;
}

// Helper to convert packed RGBA8 to Vec4<float>
static Vec4<float> unpack_color(u32 packed) {
  return {
      static_cast<float>(packed >> 24 & 0xFF) / 255.f,
      static_cast<float>(packed >> 16 & 0xFF) / 255.f,
      static_cast<float>(packed >> 8 & 0xFF) / 255.f,
      static_cast<float>(packed & 0xFF) / 255.f,
  };
}

// Rejects a malformed XF write in release builds.
#define XF_REQUIRE(cond, msg, ...)                                                                                     \
  do {                                                                                                                 \
    if (!(cond)) UNLIKELY {                                                                                            \
      CHECK(cond, msg, ##__VA_ARGS__);                                                                                  \
      Log.warn(msg, ##__VA_ARGS__);                                                                                     \
      return true;                                                                                                     \
    }                                                                                                                  \
  } while (0)

bool copy_xf_data(u32 addr, const u8* data, u32 len, bool bigEndian) {
  auto& state = register_state();
  if (addr < 0x78) {
    // Position matrices (0x0000 - 0x0077)
    u32 mtxIdx = addr / 12;
    u32 startOffset = addr % 12;
    // We only support full writes to matrices
    XF_REQUIRE(mtxIdx < MaxPnMtx, "XF: PosMtx copy oob; mtxIdx={}", mtxIdx);
    XF_REQUIRE(startOffset == 0 && len == 12, "XF: PosMtx sub-copy unsupported: offs={}, len={}", startOffset, len);
    auto& mtx = state.pnMtx[mtxIdx].pos;
    f32* flat = reinterpret_cast<f32*>(&mtx);
    for (u32 i = 0; i < len; i++) {
      flat[i] = read_f32(data + i * 4, bigEndian);
    }
    state.stateDirty = true;
    return true;
  } else if (addr < 0x0F0) {
    // Texture matrices (0x078-0x0EF)
    u32 texBase = addr - 0x078;
    u32 mtxIdx = texBase / 12;
    u32 startOffset = texBase % 12;
    XF_REQUIRE(mtxIdx < MaxTexMtx, "XF TexMtx copy oob; mtxIdx={}", mtxIdx);
    XF_REQUIRE(startOffset == 0 && (len == 8 || len == 12), "XF TexMtx sub-copy unsupported: offs={}, len={}",
               startOffset, len);

    // Determine if 2x4 or 3x4 from count
    auto& mtx = state.texMtxs[mtxIdx];
    f32* flat = reinterpret_cast<f32*>(&mtx);
    for (u32 i = 0; i < len; i++) {
      flat[i] = read_f32(data + i * 4, bigEndian);
    }
    state.stateDirty = true;
    return true;
  } else if (addr >= 0x400 && addr < 0x45A) {
    // Normal matrices (0x400-0x459)
    u32 nrmBase = addr - 0x400;
    u32 mtxIdx = nrmBase / 9;
    u32 startOffset = nrmBase % 9;
    // We only support full writes to matrices
    XF_REQUIRE(mtxIdx < MaxPnMtx, "XF: NrmMtx copy oob; mtxIdx={}", mtxIdx);
    XF_REQUIRE(startOffset == 0 && len == 9, "XF: NrmMtx sub-copy unsupported: offs={}, len={}", startOffset, len);
    auto& mtx = state.pnMtx[mtxIdx].nrm;
    f32* flat = reinterpret_cast<f32*>(&mtx);
    for (u32 i = 0; i < len; i++) {
      u32 xfIdx = i;
      u32 row = xfIdx / 3;
      u32 col = xfIdx % 3;
      if (row < 3) {
        flat[row * 4 + col] = read_f32(data + i * 4, bigEndian);
      }
    }
    state.stateDirty = true;
    return true;
  } else if (addr >= 0x500 && addr < 0x5F0) {
    // Post-transform texture matrices (0x500-0x5EF)
    u32 ptBase = addr - 0x500;
    u32 mtxIdx = ptBase / 12;
    u32 startOffset = ptBase % 12;
    XF_REQUIRE(mtxIdx < MaxPTTexMtx, "XF: PTTexMtx copy oob; mtxIdx={}", mtxIdx);
    XF_REQUIRE(startOffset == 0 && len == 12, "XF: PTTexMtx sub-copy unsupported: offs={}, len={}", startOffset, len);
    auto& mtx = state.ptTexMtxs[mtxIdx];
    f32* flat = reinterpret_cast<f32*>(&mtx);
    for (u32 i = 0; i < len; i++) {
      flat[startOffset + i] = read_f32(data + i * 4, bigEndian);
    }
    state.stateDirty = true;
    return true;
  } else if (addr >= 0x600 && addr < 0x680) {
    // Lights (0x600-0x67F) - 8 lights, 16 values each
    u32 lightBase = addr - 0x600;
    u32 lightIdx = lightBase / 0x10;
    u32 startOffset = lightBase % 0x10;
    XF_REQUIRE(lightIdx < GX::MaxLights, "XF: Light copy oob; lightIdx={}", lightIdx);
    XF_REQUIRE(startOffset + len <= 0x10,
               "XF: Light copy that crosses across light boundaries unsupported: offs={}, len={}", startOffset, len);
    auto& light = state.lights[lightIdx];
    for (u32 i = 0; i < len; i++) {
      u32 field = startOffset + i;
      f32 val = read_f32(data + i * 4, bigEndian);
      u32 ival = read_u32(data + i * 4, bigEndian);
      switch (field) {
      case 3: // Color (packed u32)
        light.color = unpack_color(ival);
        break;
      case 4:
        light.cosAtt[0] = val;
        break; // a0
      case 5:
        light.cosAtt[1] = val;
        break; // a1
      case 6:
        light.cosAtt[2] = val;
        break; // a2
      case 7:
        light.distAtt[0] = val;
        break; // k0
      case 8:
        light.distAtt[1] = val;
        break; // k1
      case 9:
        light.distAtt[2] = val;
        break; // k2
      case 10:
        light.pos[0] = val;
        break; // px
      case 11:
        light.pos[1] = val;
        break; // py
      case 12:
        light.pos[2] = val;
        break; // pz
      case 13:
        light.dir[0] = val;
        break; // nx
      case 14:
        light.dir[1] = val;
        break; // ny
      case 15:
        light.dir[2] = val;
        break; // nz
      default:
        break; // padding (0-2)
      }
    }
    state.preparedLightsDirty = true;
    state.stateDirty = true;
    return true;
  }
  return false;
}

static void apply_xf_viewport() {
  auto& state = register_state();
  const auto& vp = state.xfViewport;
  const f32 sx = vp[0];
  const f32 sy = vp[1];
  const f32 sz = vp[2];
  const f32 ox = vp[3];
  const f32 oy = vp[4];
  const f32 oz = vp[5];
  const f32 width = sx * 2.0f;
  const f32 height = -sy * 2.0f;
  constexpr f32 z24Scale = 16777216.0f;

  set_logical_viewport({
      .left = ox - 340.0f - width / 2.0f,
      .top = oy - 340.0f - height / 2.0f,
      .width = width,
      .height = height,
      .znear = (oz - sz) / z24Scale,
      .zfar = oz / z24Scale,
  });
}

static void apply_xf_projection() {
  auto& state = register_state();
  const auto& raw = state.xfProjection;
  auto& proj = state.proj;
  proj = {};
  proj.m0[0] = raw[0];
  proj.m1[1] = raw[2];
  proj.m2[2] = raw[4];
  proj.m2[3] = raw[5];

  if (state.projType == GX_ORTHOGRAPHIC) {
    proj.m0[3] = raw[1];
    proj.m1[3] = raw[3];
    proj.m3[3] = 1.0f;
  } else {
    proj.m0[2] = raw[1];
    proj.m1[2] = raw[3];
    proj.m3[2] = -1.0f;
  }

  state.stateDirty = true;
}

// Helper to extract bit fields from a 32-bit register
inline static u32 bp_get(u32 reg, u32 size, u32 shift) { return reg >> shift & (1u << size) - 1; }

static u8 normal_frac_bits(GXCompType type) {
  switch (type) {
  case GX_U8:
    return 7;
  case GX_S8:
    return 6;
  case GX_U16:
    return 15;
  case GX_S16:
    return 14;
  default:
    return 0;
  }
}

static void refresh_copy_filter_flags() {
  auto& state = register_state();
  bool aa = false;
  for (const auto& sample : state.copyFilterSamplePattern) {
    aa |= sample[0] != 6 || sample[1] != 6;
  }
  state.copyFilterAa = aa;

  static constexpr std::array<u8, 7> DefaultVFilter{0, 0, 21, 22, 21, 0, 0};
  bool vf = false;
  for (size_t i = 0; i < DefaultVFilter.size(); ++i) {
    vf |= state.copyFilterVFilter[i] != DefaultVFilter[i];
  }
  state.copyFilterVf = vf;
}

// BP register handler - decodes BP (RAS/pixel engine) register writes and updates register_state()
void handle_bp(u32 value, bool bigEndian) {
  auto& state = register_state();
  u32 regId = (value >> 24) & 0xFF;
  // Mask off the register ID from the value for field extraction
  // (the regId is stored in bits 24-31, data is in bits 0-23)

  if (regId == 0xFE) {
    state.bpRegCache[regId] = value & 0x00FFFFFF;
    return;
  } else {
    const u32 ssMask = state.bpRegCache[0xFE];
    // A preceding 0xFE write is rare; the common path only has to prove the mask is already wide open.
    if (ssMask != 0x00FFFFFF) UNLIKELY {
      state.bpRegCache[0xFE] = 0x00FFFFFF;
    }
    const u32 merged = (state.bpRegCache[regId] & ~ssMask) | (value & ssMask);
    value = (regId << 24) | (merged & 0x00FFFFFF);
    if (state.bpRegCache[regId] == value) return;
    state.bpRegCache[regId] = value;
  }
  // TEV color combiner stages (0xC0, 0xC2, 0xC4, ... 0xDE)
  if (regId >= 0xC0 && regId <= 0xDE && (regId & 1) == 0) {
    u32 stage = (regId - 0xC0) / 2;
    if (stage < MaxTevStages) {
      auto& s = state.tevStages[stage];
      s.colorPass.d = static_cast<GXTevColorArg>(bp_get(value, 4, 0));
      s.colorPass.c = static_cast<GXTevColorArg>(bp_get(value, 4, 4));
      s.colorPass.b = static_cast<GXTevColorArg>(bp_get(value, 4, 8));
      s.colorPass.a = static_cast<GXTevColorArg>(bp_get(value, 4, 12));
      s.colorOp.clamp = bp_get(value, 1, 19) != 0;
      s.colorOp.outReg = static_cast<GXTevRegID>(bp_get(value, 2, 22));
      if (bp_get(value, 2, 16) == 3) {
        // Bias==3 means compare mode: reconstruct GXTevOp enum (8 + 3-bit hw value)
        u32 hwOp = bp_get(value, 1, 18) | (bp_get(value, 2, 20) << 1);
        s.colorOp.op = static_cast<GXTevOp>(hwOp + 8);
        s.colorOp.bias = GX_TB_ZERO;
        s.colorOp.scale = GX_CS_SCALE_1;
      } else {
        // Normal mode: bit18 is op (0=ADD, 1=SUB), bits16-17 is bias, bits20-21 is scale
        s.colorOp.op = static_cast<GXTevOp>(bp_get(value, 1, 18));
        s.colorOp.bias = static_cast<GXTevBias>(bp_get(value, 2, 16));
        s.colorOp.scale = static_cast<GXTevScale>(bp_get(value, 2, 20));
      }
      mark_pipeline_state_dirty();
    }
    return;
  }

  // TEV alpha combiner stages (0xC1, 0xC3, 0xC5, ... 0xDF)
  if (regId >= 0xC1 && regId <= 0xDF && (regId & 1) == 1) {
    u32 stage = (regId - 0xC1) / 2;
    if (stage < MaxTevStages) {
      auto& s = state.tevStages[stage];
      s.tevSwapRas = static_cast<GXTevSwapSel>(bp_get(value, 2, 0));
      s.tevSwapTex = static_cast<GXTevSwapSel>(bp_get(value, 2, 2));
      s.alphaPass.d = static_cast<GXTevAlphaArg>(bp_get(value, 3, 4));
      s.alphaPass.c = static_cast<GXTevAlphaArg>(bp_get(value, 3, 7));
      s.alphaPass.b = static_cast<GXTevAlphaArg>(bp_get(value, 3, 10));
      s.alphaPass.a = static_cast<GXTevAlphaArg>(bp_get(value, 3, 13));
      s.alphaOp.clamp = bp_get(value, 1, 19) != 0;
      s.alphaOp.outReg = static_cast<GXTevRegID>(bp_get(value, 2, 22));
      if (bp_get(value, 2, 16) == 3) {
        u32 hwOp = bp_get(value, 1, 18) | (bp_get(value, 2, 20) << 1);
        s.alphaOp.op = static_cast<GXTevOp>(hwOp + 8);
        s.alphaOp.bias = GX_TB_ZERO;
        s.alphaOp.scale = GX_CS_SCALE_1;
      } else {
        s.alphaOp.op = static_cast<GXTevOp>(bp_get(value, 1, 18));
        s.alphaOp.bias = static_cast<GXTevBias>(bp_get(value, 2, 16));
        s.alphaOp.scale = static_cast<GXTevScale>(bp_get(value, 2, 20));
      }
      mark_pipeline_state_dirty();
    }
    return;
  }

  switch (regId) {
  // genMode (0x00)
  case 0x00: {
    state.numTexGens = bp_get(value, 4, 0);
    state.numChans = bp_get(value, 3, 4);
    // genMode owns the same numTexGens/numChans that XF 0x3F/0x09 decode, so the XF cache can no longer vouch for those two slots.
    state.invalidateXfReg(0x3F);
    state.invalidateXfReg(0x09);
    state.numTevStages = bp_get(value, 4, 10) + 1;
    u32 hwCull = bp_get(value, 2, 14);
    // Swap front/back to match GX convention
    switch (hwCull) {
    case GX_CULL_FRONT:
      state.cullMode = GX_CULL_BACK;
      break;
    case GX_CULL_BACK:
      state.cullMode = GX_CULL_FRONT;
      break;
    default:
      state.cullMode = static_cast<GXCullMode>(hwCull);
      break;
    }
    state.numIndStages = bp_get(value, 3, 16);
    mark_pipeline_state_dirty();
    break;
  }

  // Display copy sample pattern (0x01-0x04), six 4-bit samples per BP reg.
  case 0x01:
  case 0x02:
  case 0x03:
  case 0x04: {
    const size_t first = static_cast<size_t>(regId - 0x01) * 6;
    for (size_t i = 0; i < 6; ++i) {
      const size_t sample = first + i;
      state.copyFilterSamplePattern[sample / 2][sample % 2] = static_cast<u8>(bp_get(value, 4, i * 4));
    }
    refresh_copy_filter_flags();
    break;
  }

  // Indirect texture mask (0x0F).
  case 0x0F:
    state.indTexMask = static_cast<u8>(value & 0xFF);
    state.stateDirty = true;
    break;

  // TEV indirect stages (0x10-0x1F)
  case 0x10:
  case 0x11:
  case 0x12:
  case 0x13:
  case 0x14:
  case 0x15:
  case 0x16:
  case 0x17:
  case 0x18:
  case 0x19:
  case 0x1A:
  case 0x1B:
  case 0x1C:
  case 0x1D:
  case 0x1E:
  case 0x1F: {
    u32 stage = regId - 0x10;
    if (stage < MaxTevStages) {
      auto& s = state.tevStages[stage];
      s.indTexStage = static_cast<GXIndTexStageID>(bp_get(value, 2, 0));
      s.indTexFormat = static_cast<GXIndTexFormat>(bp_get(value, 2, 2));
      s.indTexBiasSel = static_cast<GXIndTexBiasSel>(bp_get(value, 3, 4));
      s.indTexAlphaSel = static_cast<GXIndTexAlphaSel>(bp_get(value, 2, 7));
      s.indTexMtxId = static_cast<GXIndTexMtxID>(bp_get(value, 4, 9));
      s.indTexWrapS = static_cast<GXIndTexWrap>(bp_get(value, 3, 13));
      s.indTexWrapT = static_cast<GXIndTexWrap>(bp_get(value, 3, 16));
      s.indTexUseOrigLOD = bp_get(value, 1, 19) != 0;
      s.indTexAddPrev = bp_get(value, 1, 20) != 0;
      mark_pipeline_state_dirty();
    }
    break;
  }

  // Scissor registers (0x20, 0x21)
  case 0x20:
  case 0x21: {
    const u32 scis0 = state.bpRegCache[0x20];
    const u32 scis1 = state.bpRegCache[0x21];
    const int32_t tp = static_cast<int32_t>(bp_get(scis0, 11, 0)) - 342;
    const int32_t lf = static_cast<int32_t>(bp_get(scis0, 11, 12)) - 342;
    const int32_t bm = static_cast<int32_t>(bp_get(scis1, 11, 0)) - 342;
    const int32_t rt = static_cast<int32_t>(bp_get(scis1, 11, 12)) - 342;
    const int32_t wd = std::max(rt - lf + 1, 0);
    const int32_t ht = std::max(bm - tp + 1, 0);
    set_logical_scissor({lf, tp, wd, ht});
    break;
  }

  // Line/point size (0x22)
  case 0x22: {
    state.lineWidth = static_cast<u8>(bp_get(value, 8, 0));
    state.pointSize = static_cast<u8>(bp_get(value, 8, 8));
    state.lineTexOffset = static_cast<GXTexOffset>(bp_get(value, 3, 16));
    state.pointTexOffset = static_cast<GXTexOffset>(bp_get(value, 3, 19));
    state.lineHalfAspect = bp_get(value, 1, 22) != 0;
    state.stateDirty = true;
    break;
  }

  // Indirect texture scale (0x25, 0x26)
  case 0x25: {
    if (MaxIndStages > 0) {
      state.indStages[0].scaleS = static_cast<GXIndTexScale>(bp_get(value, 4, 0));
      state.indStages[0].scaleT = static_cast<GXIndTexScale>(bp_get(value, 4, 4));
    }
    if (MaxIndStages > 1) {
      state.indStages[1].scaleS = static_cast<GXIndTexScale>(bp_get(value, 4, 8));
      state.indStages[1].scaleT = static_cast<GXIndTexScale>(bp_get(value, 4, 12));
    }
    mark_pipeline_state_dirty();
    break;
  }
  case 0x26: {
    if (MaxIndStages > 2) {
      state.indStages[2].scaleS = static_cast<GXIndTexScale>(bp_get(value, 4, 0));
      state.indStages[2].scaleT = static_cast<GXIndTexScale>(bp_get(value, 4, 4));
    }
    if (MaxIndStages > 3) {
      state.indStages[3].scaleS = static_cast<GXIndTexScale>(bp_get(value, 4, 8));
      state.indStages[3].scaleT = static_cast<GXIndTexScale>(bp_get(value, 4, 12));
    }
    mark_pipeline_state_dirty();
    break;
  }

  // Indirect texture reference (0x27)
  case 0x27: {
    for (u32 i = 0; i < MaxIndStages; i++) {
      state.indStages[i].texMapId = static_cast<GXTexMapID>(bp_get(value, 3, i * 6));
      state.indStages[i].texCoordId = static_cast<GXTexCoordID>(bp_get(value, 3, i * 6 + 3));
    }
    mark_pipeline_state_dirty();
    break;
  }

  // TEV order / tref (0x28-0x2F) - 2 stages per register
  case 0x28:
  case 0x29:
  case 0x2A:
  case 0x2B:
  case 0x2C:
  case 0x2D:
  case 0x2E:
  case 0x2F: {
    u32 idx = regId - 0x28;
    u32 stage0 = idx * 2;
    u32 stage1 = idx * 2 + 1;

    // Channel ID reverse mapping from hardware to GX
    static const GXChannelID r2c[] = {GX_COLOR0A0, GX_COLOR1A1,   GX_COLOR0A0,    GX_COLOR1A1,
                                      GX_COLOR0A0, GX_ALPHA_BUMP, GX_ALPHA_BUMPN, GX_COLOR_ZERO};

    if (stage0 < MaxTevStages) {
      auto& s = state.tevStages[stage0];
      s.texMapId = static_cast<GXTexMapID>(bp_get(value, 3, 0));
      s.texCoordId = static_cast<GXTexCoordID>(bp_get(value, 3, 3));
      // bit 6 = tex enable
      if (!bp_get(value, 1, 6)) {
        s.texMapId = GX_TEXMAP_NULL;
      }
      u32 chanHw = bp_get(value, 3, 7);
      s.channelId = (chanHw < 8) ? r2c[chanHw] : GX_COLOR_NULL;
    }
    if (stage1 < MaxTevStages) {
      auto& s = state.tevStages[stage1];
      s.texMapId = static_cast<GXTexMapID>(bp_get(value, 3, 12));
      s.texCoordId = static_cast<GXTexCoordID>(bp_get(value, 3, 15));
      if (!bp_get(value, 1, 18)) {
        s.texMapId = GX_TEXMAP_NULL;
      }
      u32 chanHw = bp_get(value, 3, 19);
      s.channelId = (chanHw < 8) ? r2c[chanHw] : GX_COLOR_NULL;
    }
    mark_pipeline_state_dirty();
    break;
  }

  // Z mode (0x40)
  case 0x40: {
    state.depthCompare = bp_get(value, 1, 0) != 0;
    state.depthFunc = static_cast<GXCompare>(bp_get(value, 3, 1));
    state.depthUpdate = bp_get(value, 1, 4) != 0;
    mark_pipeline_state_dirty();
    break;
  }

  // Blend mode / cmode0 (0x41)
  case 0x41: {
    bool blendEn = bp_get(value, 1, 0) != 0;
    bool logicEn = bp_get(value, 1, 1) != 0;
    bool dither = bp_get(value, 1, 2) != 0;
    state.colorUpdate = bp_get(value, 1, 3) != 0;
    state.alphaUpdate = bp_get(value, 1, 4) != 0;
    state.blendFacDst = static_cast<GXBlendFactor>(bp_get(value, 3, 5));
    state.blendFacSrc = static_cast<GXBlendFactor>(bp_get(value, 3, 8));
    bool subtract = bp_get(value, 1, 11) != 0;
    state.blendOp = static_cast<GXLogicOp>(bp_get(value, 4, 12));

    if (subtract) {
      state.blendMode = GX_BM_SUBTRACT;
    } else if (blendEn) {
      state.blendMode = GX_BM_BLEND;
    } else if (logicEn) {
      state.blendMode = GX_BM_LOGIC;
    } else {
      state.blendMode = GX_BM_NONE;
    }
    mark_pipeline_state_dirty();
    break;
  }

  // Dst alpha / cmode1 (0x42)
  case 0x42: {
    u8 alpha = bp_get(value, 8, 0);
    bool enabled = bp_get(value, 1, 8) != 0;
    state.dstAlpha = enabled ? alpha : UINT32_MAX;
    state.pixelFmt = decode_pixel_fmt(state.bpRegCache[0x43], value);
    mark_pipeline_state_dirty();
    break;
  }

  // PE control (0x43) - pixel format, z format, zcomp location
  case 0x43: {
    state.pixelFmt = decode_pixel_fmt(value, state.bpRegCache[0x42]);
    state.zFmt = static_cast<GXZFmt16>(bp_get(value, 3, 3));
    state.zCompLocBeforeTex = bp_get(value, 1, 6) != 0;
    mark_pipeline_state_dirty();
    break;
  }
  case 0x44:
    state.fieldMask = value & 0x3u;
    break;

  // Bounding box clear/update registers (0x55, 0x56)
  case 0x55:
  case 0x56: {
    const u32 offset = (regId & 2u);
    state.boundingBox[offset] = static_cast<u16>(value & 0x3ffu);
    state.boundingBox[offset + 1] = static_cast<u16>((value >> 10) & 0x3ffu);
    break;
  }
  case 0x58:
    state.revBits = value & 0x00FFFFFFu;
    break;

  // Scissor box offset (0x59)
  case 0x59: {
    state.scissorOffsetX = static_cast<s32>(((value & 0x3ffu) << 1) - 0x156u);
    state.scissorOffsetY = static_cast<s32>(((value & 0xffc00u) >> 9) - 0x156u);
    set_logical_scissor(state.logicalScissor);
    break;
  }

  // TLUT load address / execute (0x64, 0x65)
  case 0x64:
    break;
  case 0x65: {
    const auto idx = bp_get(value, 10, 0);
    if (idx < MaxTluts) {
      auto& slot = state.loadedTluts[idx];
      slot.loadTlut0 = state.bpRegCache[0x64];
      slot.numEntries = static_cast<u16>(bp_get(value, 10, 10) + 1);
    }
    break;
  }
  case 0x68:
    state.fieldMode = value & 0x1u;
    break;

  // Alpha compare (0xF3)
  case 0xF3: {
    state.alphaCompare.ref0 = bp_get(value, 8, 0);
    state.alphaCompare.ref1 = bp_get(value, 8, 8);
    state.alphaCompare.comp0 = static_cast<GXCompare>(bp_get(value, 3, 16));
    state.alphaCompare.comp1 = static_cast<GXCompare>(bp_get(value, 3, 19));
    state.alphaCompare.op = static_cast<GXAlphaOp>(bp_get(value, 2, 22));
    mark_pipeline_state_dirty();
    break;
  }

  // TEV K color/alpha select (0xF6-0xFD)
  case 0xF6:
  case 0xF7:
  case 0xF8:
  case 0xF9:
  case 0xFA:
  case 0xFB:
  case 0xFC:
  case 0xFD: {
    u32 kselIdx = regId - 0xF6;
    // Swap table entries (packed into pairs of ksel registers)
    if (kselIdx < MaxTevSwap * 2) {
      u32 swapIdx = kselIdx / 2;
      if (kselIdx & 1) {
        state.tevSwapTable[swapIdx].blue = static_cast<GXTevColorChan>(bp_get(value, 2, 0));
        state.tevSwapTable[swapIdx].alpha = static_cast<GXTevColorChan>(bp_get(value, 2, 2));
      } else {
        state.tevSwapTable[swapIdx].red = static_cast<GXTevColorChan>(bp_get(value, 2, 0));
        state.tevSwapTable[swapIdx].green = static_cast<GXTevColorChan>(bp_get(value, 2, 2));
      }
    }
    // K color/alpha selection for 2 stages per register
    u32 stage0 = kselIdx * 2;
    u32 stage1 = kselIdx * 2 + 1;
    if (stage0 < MaxTevStages) {
      state.tevStages[stage0].kcSel = static_cast<GXTevKColorSel>(bp_get(value, 5, 4));
      state.tevStages[stage0].kaSel = static_cast<GXTevKAlphaSel>(bp_get(value, 5, 9));
    }
    if (stage1 < MaxTevStages) {
      state.tevStages[stage1].kcSel = static_cast<GXTevKColorSel>(bp_get(value, 5, 14));
      state.tevStages[stage1].kaSel = static_cast<GXTevKAlphaSel>(bp_get(value, 5, 19));
    }
    mark_pipeline_state_dirty();
    break;
  }

  // Fog A/B parameters (0xEE-0xF0)
  // FOG0 (0xEE): A parameter - sign(1)|exp(8)|mantissa(11) partial IEEE 754 float
  case 0xEE: {
    state.fog.fog0Raw = value;
    u32 a_mant = bp_get(value, 11, 0);
    u32 a_exp = bp_get(value, 8, 11);
    u32 a_sign = bp_get(value, 1, 19);
    u32 a_bits = (a_sign << 31) | (a_exp << 23) | (a_mant << 12);
    std::memcpy(&state.fog.aRaw, &a_bits, sizeof(state.fog.aRaw));
    u32 b_s = state.fog.fog2Raw & 0x1F;
    state.fog.a = std::ldexp(state.fog.aRaw, static_cast<int>(b_s));
    state.stateDirty = true;
    break;
  }
  // FOG1 (0xEF): B mantissa (24-bit)
  case 0xEF: {
    state.fog.fog1Raw = value;
    state.fog.bMagnitude = bp_get(value, 24, 0);
    u32 b_s = state.fog.fog2Raw & 0x1F;
    float B_mant = static_cast<float>(state.fog.bMagnitude) / 8388638.0f;
    state.fog.b = std::ldexp(B_mant, static_cast<int>(b_s) - 1);
    state.stateDirty = true;
    break;
  }
  // FOG2 (0xF0): B shift/exponent (5-bit)
  case 0xF0: {
    state.fog.fog2Raw = value;
    u32 b_s = bp_get(value, 5, 0);
    state.fog.bShift = b_s;
    u32 a_mant = bp_get(state.fog.fog0Raw, 11, 0);
    u32 a_exp = bp_get(state.fog.fog0Raw, 8, 11);
    u32 a_sign = bp_get(state.fog.fog0Raw, 1, 19);
    u32 a_bits = (a_sign << 31) | (a_exp << 23) | (a_mant << 12);
    std::memcpy(&state.fog.aRaw, &a_bits, sizeof(state.fog.aRaw));
    state.fog.a = std::ldexp(state.fog.aRaw, static_cast<int>(b_s));
    state.fog.bMagnitude = bp_get(state.fog.fog1Raw, 24, 0);
    float B_mant = static_cast<float>(state.fog.bMagnitude) / 8388638.0f;
    state.fog.b = std::ldexp(B_mant, static_cast<int>(b_s) - 1);
    state.stateDirty = true;
    break;
  }

  // Fog type + C parameter from FOG3 (0xF1)
  case 0xF1: {
    const u32 fogFunc = bp_get(value, 3, 21);
    const u32 fogProj = bp_get(value, 1, 20);
    GXFogType fogType = static_cast<GXFogType>(fogFunc | (fogProj << 3));
    state.fog.type = fogType;
    // Decode C parameter (same partial float encoding as A)
    u32 c_mant = bp_get(value, 11, 0);
    u32 c_exp = bp_get(value, 8, 11);
    u32 c_sign = bp_get(value, 1, 19);
    u32 c_bits = (c_sign << 31) | (c_exp << 23) | (c_mant << 12);
    std::memcpy(&state.fog.c, &c_bits, sizeof(state.fog.c));
    mark_pipeline_state_dirty();
    break;
  }

  // Fog color from FOGCLR (0xF2)
  case 0xF2: {
    u8 b = bp_get(value, 8, 0);
    u8 g = bp_get(value, 8, 8);
    u8 r = bp_get(value, 8, 16);
    state.fog.color = {
        static_cast<float>(r) / 255.f,
        static_cast<float>(g) / 255.f,
        static_cast<float>(b) / 255.f,
        1.f,
    };
    state.stateDirty = true;
    break;
  }

  // TEV and K color registers (0xE0-0xE7): even are RA, odd are BG.
  // Bit 23 selects a K color register over a TEV color register.
  case 0xE0:
  case 0xE1:
  case 0xE2:
  case 0xE3:
  case 0xE4:
  case 0xE5:
  case 0xE6:
  case 0xE7: {
    u32 idx = (regId - 0xE0) / 2;
    bool isRA = (regId & 1) == 0;
    bool isKColor = bp_get(value, 1, 23) != 0;

    if (isKColor) {
      // K color register (8-bit components)
      if (idx < GX_MAX_KCOLOR) {
        auto& kc = state.kcolors[idx];
        if (isRA) {
          kc[0] = static_cast<float>(bp_get(value, 8, 0)) / 255.f;  // R
          kc[3] = static_cast<float>(bp_get(value, 8, 12)) / 255.f; // A
        } else {
          kc[2] = static_cast<float>(bp_get(value, 8, 0)) / 255.f;  // B
          kc[1] = static_cast<float>(bp_get(value, 8, 12)) / 255.f; // G
        }
        state.stateDirty = true;
      }
    } else {
      // TEV color register (11-bit signed components)
      if (idx < MaxTevRegs) {
        auto& cr = state.colorRegs[idx];
        if (isRA) {
          // 11-bit signed: sign-extend from 11 bits
          s32 r = bp_get(value, 11, 0);
          if (r & 0x400)
            r |= ~0x7FF; // sign extend
          s32 a = bp_get(value, 11, 12);
          if (a & 0x400)
            a |= ~0x7FF;
          cr[0] = static_cast<float>(r) / 255.f;
          cr[3] = static_cast<float>(a) / 255.f;
        } else {
          s32 b = bp_get(value, 11, 0);
          if (b & 0x400)
            b |= ~0x7FF;
          s32 g = bp_get(value, 11, 12);
          if (g & 0x400)
            g |= ~0x7FF;
          cr[2] = static_cast<float>(b) / 255.f;
          cr[1] = static_cast<float>(g) / 255.f;
        }
        state.stateDirty = true;
      }
    }
    break;
  }

  // Indirect texture matrices (0x06-0x0E), three consecutive registers per 3x2 matrix:
  // matrix 0 at 0x06, matrix 1 at 0x09, matrix 2 at 0x0C.
  case 0x06:
  case 0x07:
  case 0x08:
  case 0x09:
  case 0x0A:
  case 0x0B:
  case 0x0C:
  case 0x0D:
  case 0x0E: {
    u32 idx = (regId - 0x06) / 3;    // matrix index (0-2)
    u32 column = (regId - 0x06) % 3; // column index (0-2)
    auto& info = state.indTexMtxs[idx];

    // Decode one packed matrix column: [m[0][column], m[1][column]].
    s32 col0 = bp_get(value, 11, 0);
    if (col0 & 0x400)
      col0 |= ~0x7FF; // sign-extend from 11 bits
    s32 col1 = bp_get(value, 11, 11);
    if (col1 & 0x400)
      col1 |= ~0x7FF;

    auto& packedColumn = column == 0 ? info.mtx.m0 : (column == 1 ? info.mtx.m1 : info.mtx.m2);
    packedColumn.x = static_cast<float>(col0) / 1024.0f;
    packedColumn.y = static_cast<float>(col1) / 1024.0f;

    // Accumulate the indirect matrix scale exponent. The SDK writes two bits per column, but the
    // hardware ignores the third column's top bit, leaving 5 bits for adjScale = scaleExp + 17.
    u32 scaleBits = bp_get(value, 2, 22);
    u32 shift = column * 2;
    if (column == 2) {
      info.adjScaleRaw = (info.adjScaleRaw & ~(1u << shift)) | ((scaleBits & 1u) << shift);
    } else {
      info.adjScaleRaw = (info.adjScaleRaw & ~(3u << shift)) | (scaleBits << shift);
    }
    info.scaleExp = static_cast<s8>(info.adjScaleRaw) - 17;

    state.stateDirty = true;
    break;
  }

  // SU texture coordinate scale registers (0x30-0x3F): even (suTs0) carry S-axis scale, bias, cyl
  // wrap and line/point offset; odd (suTs1) carry the T-axis equivalents.
  case 0x30:
  case 0x31:
  case 0x32:
  case 0x33:
  case 0x34:
  case 0x35:
  case 0x36:
  case 0x37:
  case 0x38:
  case 0x39:
  case 0x3A:
  case 0x3B:
  case 0x3C:
  case 0x3D:
  case 0x3E:
  case 0x3F: {
    u32 coordIdx = (regId - 0x30) / 2;
    bool isT = (regId & 1) != 0;
    auto& tcs = state.texCoordScales[coordIdx];
    if (isT) {
      tcs.scaleT = static_cast<u16>(bp_get(value, 16, 0));
      tcs.biasT = bp_get(value, 1, 16) != 0;
      tcs.cylWrapT = bp_get(value, 1, 17) != 0;
    } else {
      tcs.scaleS = static_cast<u16>(bp_get(value, 16, 0));
      tcs.biasS = bp_get(value, 1, 16) != 0;
      tcs.cylWrapS = bp_get(value, 1, 17) != 0;
      tcs.lineOffset = bp_get(value, 1, 18) != 0;
      tcs.pointOffset = bp_get(value, 1, 19) != 0;
    }
    state.stateDirty = true;
    break;
  }

  // Copy clear color (0x4F-0x50) and depth (0x51)
  case 0x49: {
    state.dispCopySrc.x = static_cast<int32_t>(bp_get(value, 10, 0));
    state.dispCopySrc.y = static_cast<int32_t>(bp_get(value, 10, 10));
    break;
  }
  case 0x4A: {
    state.dispCopySrc.width = static_cast<int32_t>(bp_get(value, 10, 0) + 1u);
    state.dispCopySrc.height = static_cast<int32_t>(bp_get(value, 10, 10) + 1u);
    break;
  }
  case 0x4D: {
    state.dispCopyDstWidth = static_cast<u16>(bp_get(value, 10, 0) << 4);
    break;
  }
  case 0x4E: {
    const u32 iScale = bp_get(value, 9, 0);
    if (iScale != 0) {
      state.dispCopyYScale = 256.f / static_cast<float>(iScale);
    }
    break;
  }
  case 0x4F: {
    u8 r = bp_get(value, 8, 0);
    u8 a = bp_get(value, 8, 8);
    state.clearColor[0] = static_cast<float>(r) / 255.f;
    state.clearColor[3] = static_cast<float>(a) / 255.f;
    state.stateDirty = true;
    break;
  }
  case 0x50: {
    u8 b = bp_get(value, 8, 0);
    u8 g = bp_get(value, 8, 8);
    state.clearColor[2] = static_cast<float>(b) / 255.f;
    state.clearColor[1] = static_cast<float>(g) / 255.f;
    state.stateDirty = true;
    break;
  }
  case 0x51: {
    state.clearDepth = bp_get(value, 24, 0);
    state.stateDirty = true;
    break;
  }
  case 0xF4: {
    state.zTextureBias = value & 0x00FFFFFFu;
    mark_pipeline_state_dirty();
    break;
  }
  case 0xF5: {
    state.zTextureFmt = static_cast<u8>(bp_get(value, 2, 0));
    state.zTextureOp = static_cast<GXZTexOp>(bp_get(value, 2, 2));
    mark_pipeline_state_dirty();
    break;
  }
  case 0x52: {
    state.copyClamp = static_cast<GXFBClamp>(bp_get(value, 2, 0));
    state.texCopyFmt = static_cast<GXTexFmt>(bp_get(value, 4, 3));
    state.dispCopyGamma = static_cast<GXGamma>(bp_get(value, 2, 7));
    state.texCopyHalfScale = bp_get(value, 1, 9) != 0;
    state.dispCopyFrame2Field = bp_get(value, 2, 12);
    break;
  }
  case 0x53: {
    state.copyFilterVFilter[0] = static_cast<u8>(bp_get(value, 6, 0));
    state.copyFilterVFilter[1] = static_cast<u8>(bp_get(value, 6, 6));
    state.copyFilterVFilter[2] = static_cast<u8>(bp_get(value, 6, 12));
    state.copyFilterVFilter[3] = static_cast<u8>(bp_get(value, 6, 18));
    refresh_copy_filter_flags();
    break;
  }
  case 0x54: {
    state.copyFilterVFilter[4] = static_cast<u8>(bp_get(value, 6, 0));
    state.copyFilterVFilter[5] = static_cast<u8>(bp_get(value, 6, 6));
    state.copyFilterVFilter[6] = static_cast<u8>(bp_get(value, 6, 12));
    refresh_copy_filter_flags();
    break;
  }
  case 0xE8:
  case 0xE9:
  case 0xEA:
  case 0xEB:
  case 0xEC:
  case 0xED:
    state.fogRange[regId - 0xE8] = value & 0x00FFFFFFu;
    mark_pipeline_state_dirty();
    break;

  default:
    if (const auto mapping = decode_tex_bp_reg(regId); mapping.has_value()) {
      auto& slot = state.loadedTextures[mapping->texMapId];
      bool changed = false;
      switch (mapping->kind) {
      case TexBpRegMapping::Kind::Mode0:
        changed = slot.mode0 != value;
        if (changed) {
          slot.mode0 = value;
        }
        break;
      case TexBpRegMapping::Kind::Mode1:
        changed = slot.mode1 != value;
        if (changed) {
          slot.mode1 = value;
        }
        break;
      case TexBpRegMapping::Kind::Image0:
        changed = slot.image0 != value;
        if (changed) {
          slot.image0 = value;
          slot.mWidth = 0;
          slot.mHeight = 0;
          slot.mFormat = gfx::InvalidTextureFormat;
        }
        break;
      case TexBpRegMapping::Kind::Image3:
        changed = slot.image3 != value;
        if (changed) {
          slot.image3 = value;
        }
        break;
      case TexBpRegMapping::Kind::Tlut:
        // TLUT region's TMEM offset
        break;
      case TexBpRegMapping::Kind::Image1:
      case TexBpRegMapping::Kind::Image2:
        // GXTexRegion regs
        break;
      }
      if (changed) {
        state.stateDirty = true;
      }
    } else {
#ifndef NDEBUG
      Log.debug("Unhandled BP register 0x{:02X} (value 0x{:06X})", regId, value & 0xFFFFFF);
#endif
    }
    break;
  }
}

extern "C" void GXApplyBPReg(u8 reg, u32 value) {
  handle_bp((static_cast<u32>(reg) << 24) | (value & 0x00FFFFFFu), true);
}

static bool cacheable_cp_register(u8 addr) {
  return addr == 0x30 || addr == 0x40 || addr == 0x50 || addr == 0x60 || (addr >= 0x70 && addr <= 0x97);
}

static std::array<u32, 0x100> s_cpRegisterCache{};
static std::array<bool, 0x100> s_cpRegisterCacheValid{};

void reset_cp_register_cache() {
  s_cpRegisterCache.fill(0);
  s_cpRegisterCacheValid.fill(false);
}

static bool cp_register_write_unchanged(u8 addr, u32 value) {
  if (!cacheable_cp_register(addr)) return false;

  if (s_cpRegisterCacheValid[addr] && s_cpRegisterCache[addr] == value) {
    return true;
  }
  s_cpRegisterCacheValid[addr] = true;
  s_cpRegisterCache[addr] = value;
  return false;
}

// CP register handler - decodes CP register writes and updates register_state()
void handle_cp(u8 addr, u32 value, bool bigEndian) {
  auto& state = register_state();
  if (cp_register_write_unchanged(addr, value)) return;

  switch (addr) {
  // VCD low (0x50)
  case 0x50: {
    auto& vd = state.vtxDesc;
    auto& svd = state.sourceVtxDesc;
    vd[GX_VA_PNMTXIDX] = static_cast<GXAttrType>(bp_get(value, 1, 0));
    vd[GX_VA_TEX0MTXIDX] = static_cast<GXAttrType>(bp_get(value, 1, 1));
    vd[GX_VA_TEX1MTXIDX] = static_cast<GXAttrType>(bp_get(value, 1, 2));
    vd[GX_VA_TEX2MTXIDX] = static_cast<GXAttrType>(bp_get(value, 1, 3));
    vd[GX_VA_TEX3MTXIDX] = static_cast<GXAttrType>(bp_get(value, 1, 4));
    vd[GX_VA_TEX4MTXIDX] = static_cast<GXAttrType>(bp_get(value, 1, 5));
    vd[GX_VA_TEX5MTXIDX] = static_cast<GXAttrType>(bp_get(value, 1, 6));
    vd[GX_VA_TEX6MTXIDX] = static_cast<GXAttrType>(bp_get(value, 1, 7));
    vd[GX_VA_TEX7MTXIDX] = static_cast<GXAttrType>(bp_get(value, 1, 8));
    vd[GX_VA_POS] = static_cast<GXAttrType>(bp_get(value, 2, 9));
    vd[GX_VA_NRM] = static_cast<GXAttrType>(bp_get(value, 2, 11));
    vd[GX_VA_CLR0] = static_cast<GXAttrType>(bp_get(value, 2, 13));
    vd[GX_VA_CLR1] = static_cast<GXAttrType>(bp_get(value, 2, 15));
    for (int attr = GX_VA_PNMTXIDX; attr <= GX_VA_CLR1; ++attr) {
      svd[attr] = vd[attr];
    }
    mark_pipeline_state_dirty();
    state.clearVtxSizeCache();
    break;
  }

  // VCD high (0x60)
  case 0x60: {
    auto& vd = state.vtxDesc;
    auto& svd = state.sourceVtxDesc;
    vd[GX_VA_TEX0] = static_cast<GXAttrType>(bp_get(value, 2, 0));
    vd[GX_VA_TEX1] = static_cast<GXAttrType>(bp_get(value, 2, 2));
    vd[GX_VA_TEX2] = static_cast<GXAttrType>(bp_get(value, 2, 4));
    vd[GX_VA_TEX3] = static_cast<GXAttrType>(bp_get(value, 2, 6));
    vd[GX_VA_TEX4] = static_cast<GXAttrType>(bp_get(value, 2, 8));
    vd[GX_VA_TEX5] = static_cast<GXAttrType>(bp_get(value, 2, 10));
    vd[GX_VA_TEX6] = static_cast<GXAttrType>(bp_get(value, 2, 12));
    vd[GX_VA_TEX7] = static_cast<GXAttrType>(bp_get(value, 2, 14));
    for (int attr = GX_VA_TEX0; attr <= GX_VA_TEX7; ++attr) {
      svd[attr] = vd[attr];
    }
    mark_pipeline_state_dirty();
    state.clearVtxSizeCache();
    break;
  }

  // Matrix index A (0x30)
  case 0x30: {
    state.currentPnMtx = bp_get(value, 6, 0) / 3;
    for (u32 i = 0; i < 4 && i < MaxTexCoord; i++) {
      auto texMtx = static_cast<GXTexMtx>(bp_get(value, 6, 6 + i * 6));
      assert(texMtx >= 0 && texMtx <= GXTexMtx::GX_IDENTITY);
      state.tcgs[i].mtx = texMtx;
    }
    // Same matrix indices as XF 0x18, written from a different bank.
    state.invalidateXfReg(0x18);
    mark_pipeline_state_dirty();
    break;
  }

  // Matrix index B (0x40)
  case 0x40: {
    for (u32 i = 0; i < 4 && (i + 4) < MaxTexCoord; i++) {
      auto texMtx = static_cast<GXTexMtx>(bp_get(value, 6, i * 6));
      assert(texMtx >= 0 && texMtx <= GXTexMtx::GX_IDENTITY);
      state.tcgs[i + 4].mtx = texMtx;
    }
    // Same matrix indices as XF 0x19, written from a different bank.
    state.invalidateXfReg(0x19);
    mark_pipeline_state_dirty();
    break;
  }

  default:
    // VAT A registers (0x70-0x77)
    if (addr >= 0x70 && addr <= 0x77) {
      u32 fmt = addr - 0x70;
      auto& vf = state.vtxFmts[fmt];
      vf.attrs[GX_VA_POS].cnt = static_cast<GXCompCnt>(bp_get(value, 1, 0));
      vf.attrs[GX_VA_POS].type = static_cast<GXCompType>(bp_get(value, 3, 1));
      vf.attrs[GX_VA_POS].frac = static_cast<u8>(bp_get(value, 5, 4));
      vf.attrs[GX_VA_NRM].type = static_cast<GXCompType>(bp_get(value, 3, 10));
      if (bp_get(value, 1, 31) != 0) {
        vf.attrs[GX_VA_NRM].cnt = GX_NRM_NBT3;
      } else {
        vf.attrs[GX_VA_NRM].cnt = bp_get(value, 1, 9) != 0 ? GX_NRM_NBT : GX_NRM_XYZ;
      }
      vf.attrs[GX_VA_NRM].frac = normal_frac_bits(vf.attrs[GX_VA_NRM].type);
      vf.attrs[GX_VA_CLR0].cnt = static_cast<GXCompCnt>(bp_get(value, 1, 13));
      vf.attrs[GX_VA_CLR0].type = static_cast<GXCompType>(bp_get(value, 3, 14));
      vf.attrs[GX_VA_CLR0].frac = 0;
      vf.attrs[GX_VA_CLR1].cnt = static_cast<GXCompCnt>(bp_get(value, 1, 17));
      vf.attrs[GX_VA_CLR1].type = static_cast<GXCompType>(bp_get(value, 3, 18));
      vf.attrs[GX_VA_CLR1].frac = 0;
      vf.attrs[GX_VA_TEX0].cnt = static_cast<GXCompCnt>(bp_get(value, 1, 21));
      vf.attrs[GX_VA_TEX0].type = static_cast<GXCompType>(bp_get(value, 3, 22));
      vf.attrs[GX_VA_TEX0].frac = static_cast<u8>(bp_get(value, 5, 25));
      mark_pipeline_state_dirty();
      state.clearVtxSizeCache();
    }
    // VAT B registers (0x80-0x87)
    else if (addr >= 0x80 && addr <= 0x87) {
      u32 fmt = addr - 0x80;
      auto& vf = state.vtxFmts[fmt];
      vf.attrs[GX_VA_TEX1].cnt = static_cast<GXCompCnt>(bp_get(value, 1, 0));
      vf.attrs[GX_VA_TEX1].type = static_cast<GXCompType>(bp_get(value, 3, 1));
      vf.attrs[GX_VA_TEX1].frac = static_cast<u8>(bp_get(value, 5, 4));
      vf.attrs[GX_VA_TEX2].cnt = static_cast<GXCompCnt>(bp_get(value, 1, 9));
      vf.attrs[GX_VA_TEX2].type = static_cast<GXCompType>(bp_get(value, 3, 10));
      vf.attrs[GX_VA_TEX2].frac = static_cast<u8>(bp_get(value, 5, 13));
      vf.attrs[GX_VA_TEX3].cnt = static_cast<GXCompCnt>(bp_get(value, 1, 18));
      vf.attrs[GX_VA_TEX3].type = static_cast<GXCompType>(bp_get(value, 3, 19));
      vf.attrs[GX_VA_TEX3].frac = static_cast<u8>(bp_get(value, 5, 22));
      vf.attrs[GX_VA_TEX4].cnt = static_cast<GXCompCnt>(bp_get(value, 1, 27));
      vf.attrs[GX_VA_TEX4].type = static_cast<GXCompType>(bp_get(value, 3, 28));
      // TEX4 frac is in VAT C
      mark_pipeline_state_dirty();
      state.clearVtxSizeCache();
    }
    // VAT C registers (0x90-0x97)
    else if (addr >= 0x90 && addr <= 0x97) {
      u32 fmt = addr - 0x90;
      auto& vf = state.vtxFmts[fmt];
      vf.attrs[GX_VA_TEX4].frac = static_cast<u8>(bp_get(value, 5, 0));
      vf.attrs[GX_VA_TEX5].cnt = static_cast<GXCompCnt>(bp_get(value, 1, 5));
      vf.attrs[GX_VA_TEX5].type = static_cast<GXCompType>(bp_get(value, 3, 6));
      vf.attrs[GX_VA_TEX5].frac = static_cast<u8>(bp_get(value, 5, 9));
      vf.attrs[GX_VA_TEX6].cnt = static_cast<GXCompCnt>(bp_get(value, 1, 14));
      vf.attrs[GX_VA_TEX6].type = static_cast<GXCompType>(bp_get(value, 3, 15));
      vf.attrs[GX_VA_TEX6].frac = static_cast<u8>(bp_get(value, 5, 18));
      vf.attrs[GX_VA_TEX7].cnt = static_cast<GXCompCnt>(bp_get(value, 1, 23));
      vf.attrs[GX_VA_TEX7].type = static_cast<GXCompType>(bp_get(value, 3, 24));
      vf.attrs[GX_VA_TEX7].frac = static_cast<u8>(bp_get(value, 5, 27));
      mark_pipeline_state_dirty();
      state.clearVtxSizeCache();
    }
    // Array base addresses (0xA0-0xAF)
    else if (addr >= 0xA0 && addr <= 0xAF) {
      Log.error("CP_REG_ARRAYBASE_ID is not supported on Aurora. Use GX_LOAD_AURORA_ARRAYBASE instead.");
    }
    // Array strides (0xB0-0xBF)
    else if (addr >= 0xB0 && addr <= 0xBF) {
      u32 attrIdx = addr - 0xB0 + GX_VA_POS;
      if (attrIdx < GX_VA_MAX_ATTR) {
        auto& array = state.arrays[attrIdx];
        const auto newStride = static_cast<u8>(value);
        if (array.stride != newStride) {
          array.stride = newStride;
          mark_pipeline_state_dirty();
        }
      }
    }
    break;
  }
}

// XF register handler - decodes XF (transform unit) register writes and updates register_state()
void handle_xf(const u8* data, u32& pos, u32 size, bool bigEndian) {
  auto& state = register_state();
  // These bounds must hold in release too: CHECK() is a no-op under NDEBUG, so relying on it alone let a truncated guest display list read past `data`.
  if (pos > size || size - pos < 4) UNLIKELY {
      CHECK(false, "XF header read overrun");
      pos = size;
      return;
    }
  u32 header = read_u32(data + pos, bigEndian);
  pos += 4;

  u32 count = ((header >> 16) & 0xFFFF) + 1;
  u32 addr = header & 0xFFFF;
  u32 dataBytes = count * 4;
  // Log.warn("  xf: addr {:04x} count {} dataBytes {} pos {} -> {}", addr, count, dataBytes, pos, pos + dataBytes);
  if (size - pos < dataBytes) UNLIKELY {
      CHECK(false, "XF data read overrun: need {} bytes at pos {}", dataBytes, pos);
      pos = size;
      return;
    }

  const u8* xfData = data + pos;

  if (copy_xf_data(addr, xfData, count, bigEndian)) {
    // copy_xf_data handled everything.
  } else if (addr >= 0x1000) {
    // XF registers (0x1000+)
    u32 xfAddr = addr - 0x1000;
    bool viewportUpdated = false;
    bool projectionUpdated = false;
    for (u32 i = 0; i < count; i++) {
      u32 reg = xfAddr + i;
      u32 val = read_u32(xfData + i * 4, bigEndian);

      // Skip register writes that decode to state we already hold.
      const bool cacheable = reg < state.xfRegCache.size();
      const bool unchanged = cacheable && state.xfRegMatches(reg, val);
      if (cacheable) state.storeXfReg(reg, val);
      // Viewport (0x1A-0x1F) and projection (0x20-0x26) keep their unconditional apply below; only the banks that already had skip semantics and the TexGen bank drop out here.
      if (unchanged && (reg <= 0x19 || reg >= 0x3F)) continue;

      switch (reg) {
      case 0x00:
        state.xfError = val;
        break;
      case 0x08:
        // XF vertex specs (numColors, numNormals, numTexCoords) - informational
        break;
      case 0x09:
        // numChans
        state.numChans = val;
        state.stateDirty = true;
        break;
      case 0x0A:
        // Ambient color 0
        state.colorChannelState[GX_COLOR0].ambColor = unpack_color(val);
        state.colorChannelState[GX_ALPHA0].ambColor = unpack_color(val);
        state.stateDirty = true;
        break;
      case 0x0B:
        // Ambient color 1
        state.colorChannelState[GX_COLOR1].ambColor = unpack_color(val);
        state.colorChannelState[GX_ALPHA1].ambColor = unpack_color(val);
        state.stateDirty = true;
        break;
      case 0x0C:
        // Material color 0
        state.colorChannelState[GX_COLOR0].matColor = unpack_color(val);
        state.colorChannelState[GX_ALPHA0].matColor = unpack_color(val);
        state.stateDirty = true;
        break;
      case 0x0D:
        // Material color 1
        state.colorChannelState[GX_COLOR1].matColor = unpack_color(val);
        state.colorChannelState[GX_ALPHA1].matColor = unpack_color(val);
        state.stateDirty = true;
        break;
      case 0x0E:
      case 0x0F:
      case 0x10:
      case 0x11: {
        // Channel control registers
        u32 chanId = reg - 0x0E;
        if (chanId < MaxColorChannels) {
          auto& chan = state.colorChannelConfig[chanId];
          chan.matSrc = static_cast<GXColorSrc>(bp_get(val, 1, 0));
          chan.lightingEnabled = bp_get(val, 1, 1) != 0;
          u32 lightsLo = bp_get(val, 4, 2);
          chan.ambSrc = static_cast<GXColorSrc>(bp_get(val, 1, 6));
          chan.diffFn = static_cast<GXDiffuseFn>(bp_get(val, 2, 7));
          u32 lightsHi = bp_get(val, 4, 11);
          switch (bp_get(val, 2, 9)) {
          case 1:
            chan.attnFn = GX_AF_SPEC;
            break;
          case 3:
            chan.attnFn = GX_AF_SPOT;
            break;
          case 0:
          case 2:
          default:
            chan.attnFn = GX_AF_NONE;
            break;
          }
          u32 lightMask = lightsLo | (lightsHi << 4);
          state.colorChannelState[chanId].lightMask = GX::LightMask{lightMask};
          mark_pipeline_state_dirty();
        }
        break;
      }
      case 0x12:
        state.dualTex = val;
        mark_pipeline_state_dirty();
        break;
      case 0x18: {
        // Matrix index A: PnMtx + TexCoord0-3 matrix indices
        state.currentPnMtx = bp_get(val, 6, 0) / 3;
        for (u32 i = 0; i < 4 && i < MaxTexCoord; i++) {
          auto texMtx = static_cast<GXTexMtx>(bp_get(val, 6, 6 + i * 6));
          assert(texMtx >= 0 && texMtx <= GXTexMtx::GX_IDENTITY);
          state.tcgs[i].mtx = texMtx;
        }
        mark_pipeline_state_dirty();
        break;
      }
      case 0x19: {
        // Matrix index B: TexCoord4-7 matrix indices
        for (u32 i = 0; i < 4 && (i + 4) < MaxTexCoord; i++) {
          state.tcgs[i + 4].mtx = static_cast<GXTexMtx>(bp_get(val, 6, i * 6));
        }
        mark_pipeline_state_dirty();
        break;
      }
      case 0x1A:
      case 0x1B:
      case 0x1C:
      case 0x1D:
      case 0x1E:
      case 0x1F: {
        // Viewport: sx, sy, sz, ox, oy, oz at XF 0x101A-0x101F
        const u32 vpOff = reg - 0x1A;
        state.xfViewport[vpOff] = read_f32(xfData + i * 4, bigEndian);
        viewportUpdated = true;
        break;
      }
      case 0x20:
      case 0x21:
      case 0x22:
      case 0x23:
      case 0x24:
      case 0x25:
      case 0x26: {
        // Projection: 6 params + type at XF 0x1020-0x1026
        const u32 projOff = reg - 0x20;
        if (projOff < state.xfProjection.size()) {
          state.xfProjection[projOff] = read_f32(xfData + i * 4, bigEndian);
        } else {
          state.projType = static_cast<GXProjectionType>(val);
        }
        projectionUpdated = true;
        break;
      }
      case 0x3F:
        // numTexGens
        state.numTexGens = val;
        mark_pipeline_state_dirty();
        break;
      default:
        // TexGen config (0x40-0x4F) and post-transform (0x50-0x5F)
        if (reg >= 0x40 && reg <= 0x4F) {
          u32 tcIdx = reg - 0x40;
          if (tcIdx < MaxTexCoord) {
            auto& tcg = state.tcgs[tcIdx];
            bool proj = bp_get(val, 1, 1) != 0;
            u32 form = bp_get(val, 1, 2);
            u32 tgType = bp_get(val, 3, 4);
            u32 srcRow = bp_get(val, 5, 7);
            tcg.inputFormAB11 = form == 0;

            if (tgType == 0) {
              tcg.type = proj ? GX_TG_MTX3x4 : GX_TG_MTX2x4;
            } else if (tgType == 1) {
              // Bump mapping
              tcg.type = static_cast<GXTexGenType>(bp_get(val, 3, 15) + 2);
            } else if (tgType == 2 || tgType == 3) {
              tcg.type = GX_TG_SRTG;
            }

            // Decode source from row
            static const GXTexGenSrc rowToSrc[] = {GX_TG_POS,  GX_TG_NRM,  GX_TG_COLOR0, GX_TG_BINRM, GX_TG_TANGENT,
                                                   GX_TG_TEX0, GX_TG_TEX1, GX_TG_TEX2,   GX_TG_TEX3,  GX_TG_TEX4,
                                                   GX_TG_TEX5, GX_TG_TEX6, GX_TG_TEX7};
            if (srcRow < 13) {
              tcg.src = rowToSrc[srcRow];
            }
            mark_pipeline_state_dirty();
          }
        } else if (reg >= 0x50 && reg <= 0x5F) {
          u32 tcIdx = reg - 0x50;
          if (tcIdx < MaxTexCoord) {
            state.tcgs[tcIdx].postMtx = static_cast<GXPTTexMtx>(bp_get(val, 6, 0) + 64);
            state.tcgs[tcIdx].normalize = bp_get(val, 1, 8) != 0;
            mark_pipeline_state_dirty();
          }
        } else {
#ifndef NDEBUG
          Log.debug("Unhandled XF register 0x{:04X} (value 0x{:08X})", reg, val);
#endif
        }
        break;
      }
    }
    if (viewportUpdated) {
      apply_xf_viewport();
    }
    if (projectionUpdated) {
      apply_xf_projection();
    }
  }

  pos += dataBytes;
}

} // namespace aurora::gx::fifo
