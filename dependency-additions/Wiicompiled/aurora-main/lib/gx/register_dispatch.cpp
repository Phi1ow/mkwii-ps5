#include "command_processor.hpp"
#include "command_reader.hpp"
#include "register_decoder.hpp"
#include "register_backend.hpp"
#include <dolphin/gx/GXAurora.h>
#include <aurora/gfx.h>
#include <tracy/Tracy.hpp>
#include <mutex>

namespace aurora::gx::fifo {
static Module Log("aurora::gx::fifo");

static bool has_bytes(u32 pos, u32 size, u32 needed) noexcept {
  return pos <= size && needed <= size - pos;
}

static bool is_draw_cmd(u8 cmd) {
  return cmd >= CP_PRIMITIVE_START && cmd <= CP_PRIMITIVE_END;
}

static bool handle_aurora(const u8* data, u32& pos, u32 size, bool bigEndian);

void process(const u8* data, u32 size, bool bigEndian) {
  ZoneScoped;
  if (data == nullptr && size != 0) { Log.warn("Null GX FIFO data"); return; }
  // Everything decoded here mutates renderer state (GX state, the recorded command lists and the mapped staging buffers), so take the renderer GPU mutex once for the whole drain rather than once per draw command.
  std::lock_guard gpuLock(aurora::renderer_gpu_mutex());
  u32 pos = 0;

  while (pos < size) {
    u8 cmd = data[pos++];
    u8 opcode = cmd & CP_OPCODE_MASK;
    // Log.warn("Processing opcode {:02x} at pos {} (size {})", opcode, pos - 1, size);

    switch (opcode) {
    case CP_CMD_NOP:
      continue;

    case CP_CMD_LOAD_BP_REG: {
      if (!has_bytes(pos, size, 4)) { Log.warn("BP reg read overrun"); return; }
      u32 value = read_u32(data + pos, bigEndian);
      pos += 4;
      handle_bp(value, bigEndian);
      break;
    }

    case CP_CMD_LOAD_CP_REG: {
      if (!has_bytes(pos, size, 5)) { Log.warn("CP reg read overrun"); return; }
      u8 addr = data[pos++];
      u32 value = read_u32(data + pos, bigEndian);
      pos += 4;
      handle_cp(addr, value, bigEndian);
      break;
    }

    case CP_CMD_LOAD_XF_REG: {
      handle_xf(data, pos, size, bigEndian);
      break;
    }

    case CP_CMD_LOAD_INDX_A:
    case CP_CMD_LOAD_INDX_B:
    case CP_CMD_LOAD_INDX_C:
    case CP_CMD_LOAD_INDX_D: {
      ZoneScopedN("LOAD_INDX");
      if (!has_bytes(pos, size, 4)) { Log.warn("indexed XF read overrun"); return; }
      const u32 value = read_u32(data + pos, bigEndian);
      pos += 4;

      const u32 arrayType = GX_POS_MTX_ARRAY + ((opcode - CP_CMD_LOAD_INDX_A) / 0x08);
      const u32 srcArrayIdx = value >> 16;
      const u16 len = static_cast<u16>(((value >> 12) & 0x0f) + 1);
      const u16 dstAddr = static_cast<u16>(value & 0x0fff);
      auto const& array = register_state().arrays[arrayType];
      const u32 byteOffset = srcArrayIdx * array.stride;
      const u32 byteCount = static_cast<u32>(len) * 4u;
      if (array.data == nullptr || array.stride == 0 || byteOffset + byteCount > array.size) {
        static u32 invalidIndexedXfLogCount = 0;
        if (invalidIndexedXfLogCount < 16) {
          Log.warn("Skipping indexed XF load with invalid source array: array={} idx={} stride={} offset={} bytes={} "
                   "size={} dst=0x{:04X}",
                   arrayType, srcArrayIdx, array.stride, byteOffset, byteCount, array.size, dstAddr);
          ++invalidIndexedXfLogCount;
        }
        break;
      }
      u8* srcData = ((u8*)array.data) + byteOffset;
      if (!copy_xf_data(dstAddr, srcData, len, bigEndian)) {
#ifndef NDEBUG
        Log.debug("Unimplemented indexed XF load (opcode 0x{:02X}, dstAddr=0x{:04X})", opcode, dstAddr);
#endif
      }
      break;
    }

    case CP_CMD_CALL_DL: {
      // Call display list: 8 bytes (address + size)
      if (!has_bytes(pos, size, 8)) { Log.warn("call DL read overrun"); return; }
      Log.warn("Ignoring nested GX_CMD_CALL_DL");
      pos += 8;
      break;
    }

    case CP_CMD_INVAL_VTX: {
      // GXInvalidateVtxCache tells the GPU that CPU-written indexed vertex arrays must be observed by subsequent draws.
      for (int i = GX_VA_POS; i <= GX_VA_TEX7; ++i) {
        register_state().arrays[i].cachedRange = {};
      }
      break;
    }

    case GX_LOAD_AURORA: {
      if (!handle_aurora(data, pos, size, bigEndian)) {
        return;
      }
      break;
    }

    default:
      // Draw commands occupy the full 0x80-0xBF range.
      if (is_draw_cmd(cmd)) {
        if (!handle_draw(cmd, data, pos, size, bigEndian)) {
          return;
        }
      } else {
        static u32 unknownLogCount = 0;
        if (unknownLogCount < 16) {
          // Hex dump surrounding bytes for debugging
          u32 dumpStart = (pos > 17) ? pos - 17 : 0;
          u32 dumpEnd = (pos + 16 < size) ? pos + 16 : size;
          std::string hex;
          for (u32 i = dumpStart; i < dumpEnd; i++) {
            if (i == pos - 1)
              hex += fmt::format("[{:02x}]", data[i]);
            else
              hex += fmt::format(" {:02x}", data[i]);
          }
          Log.warn("  hex dump (pos {}-{}):{}", dumpStart, dumpEnd - 1, hex);
          Log.warn("command_processor: unknown opcode 0x{:02X} at pos {}", cmd, pos - 1);
          ++unknownLogCount;
        }
      }
      break;
    }
  }
}

static bool read_string(const u8* data, u32& pos, u32 size, bool bigEndian, std::string& str) {
  if (!has_bytes(pos, size, 2)) return false;
  const u16 length = read_u16(data + pos, bigEndian);
  pos += 2;
  if (!has_bytes(pos, size, length)) return false;
  str.assign(reinterpret_cast<const char*>(data) + pos, length);
  pos += length;
  return true;
}

bool handle_aurora(const u8* data, u32& pos, u32 size, bool bigEndian) {
  ZoneScoped;
  if (!has_bytes(pos, size, 2)) {
    return false;
  }
  u16 subCmd = read_u16(data + pos, bigEndian);
  pos += 2;

  // Setting of vertex array bases.
  if (subCmd == GX_LOAD_AURORA_VIEWPORT_RENDER) {
    if (!has_bytes(pos, size, 24)) { Log.warn("GX_LOAD_AURORA_VIEWPORT_RENDER read overrun"); return false; }
    const f32 left = read_f32(data + pos, bigEndian);
    pos += 4;
    const f32 top = read_f32(data + pos, bigEndian);
    pos += 4;
    const f32 width = read_f32(data + pos, bigEndian);
    pos += 4;
    const f32 height = read_f32(data + pos, bigEndian);
    pos += 4;
    const f32 nearZ = read_f32(data + pos, bigEndian);
    pos += 4;
    const f32 farZ = read_f32(data + pos, bigEndian);
    pos += 4;
    set_render_viewport({
        .left = left,
        .top = top,
        .width = width,
        .height = height,
        .znear = nearZ,
        .zfar = farZ,
    });
  } else if (subCmd == GX_LOAD_AURORA_SCISSOR_RENDER) {
    if (!has_bytes(pos, size, 16)) { Log.warn("GX_LOAD_AURORA_SCISSOR_RENDER read overrun"); return false; }
    const int32_t left = static_cast<int32_t>(read_u32(data + pos, bigEndian));
    pos += 4;
    const int32_t top = static_cast<int32_t>(read_u32(data + pos, bigEndian));
    pos += 4;
    const int32_t width = static_cast<int32_t>(read_u32(data + pos, bigEndian));
    pos += 4;
    const int32_t height = static_cast<int32_t>(read_u32(data + pos, bigEndian));
    pos += 4;
    set_render_scissor({left, top, width, height});
  } else if (subCmd >= GX_LOAD_AURORA_ARRAYBASE && subCmd <= (GX_LOAD_AURORA_ARRAYBASE | 0x0f)) {
    if (!has_bytes(pos, size, 13)) { Log.warn("GX_LOAD_AURORA_ARRAYBASE read overrun"); return false; }
    u32 attrIdx = subCmd - GX_LOAD_AURORA_ARRAYBASE + GX_VA_POS;

    u64 arrayAddr = read_u64(data + pos, bigEndian);
    pos += 8;
    u32 arraySize = read_u32(data + pos, bigEndian);
    pos += 4;
    bool le = data[pos] == 1;
    pos += 1;

    auto& array = register_state().arrays[attrIdx];
    const auto newData = reinterpret_cast<void*>(arrayAddr);
    if (array.data != newData || array.size != arraySize || array.le != le) {
      array.data = newData;
      array.size = arraySize;
      array.le = le;
      // Only drop the cached upload when the backing array actually changes.
      array.cachedRange = {};
      mark_pipeline_state_dirty();
    }
  } else if (subCmd == GX_LOAD_AURORA_TEXOBJ) {
    if (!has_bytes(pos, size, 34)) { Log.warn("GX_LOAD_AURORA_TEXOBJ read overrun"); return false; }
    const auto texMapId = data[pos];
    pos += 1;
    if (texMapId >= MaxTextures) { Log.warn("invalid texture map id {}", texMapId); return false; }
    auto& slot = register_state().loadedTextures[texMapId];
    GXTexObj_ next = slot;
    next.data = reinterpret_cast<const void*>(read_u64(data + pos, bigEndian));
    pos += 8;
    next.mWidth = read_u32(data + pos, bigEndian);
    pos += 4;
    next.mHeight = read_u32(data + pos, bigEndian);
    pos += 4;
    next.mFormat = static_cast<GXTexFmt>(read_u32(data + pos, bigEndian));
    pos += 4;
    next.tlut = static_cast<GXTlut>(read_u32(data + pos, bigEndian));
    pos += 4;
    if (data[pos] != 0) {
      next.flags |= 1u;
    } else {
      next.flags &= ~1u;
    }
    pos += 1;
    next.texObjId = read_u32(data + pos, bigEndian);
    pos += 4;
    next.texDataVersion = read_u32(data + pos, bigEndian);
    pos += 4;
    next.set_no_cache(false); // Reset no-cache flag
    const bool changed = slot.data != next.data || slot.mWidth != next.mWidth || slot.mHeight != next.mHeight ||
                         slot.mFormat != next.mFormat || slot.tlut != next.tlut || slot.flags != next.flags ||
                         slot.texObjId != next.texObjId || slot.texDataVersion != next.texDataVersion;
    slot = next;
    if (changed) {
      register_state().stateDirty = true;
    }
  } else if (subCmd == GX_LOAD_AURORA_TLUT) {
    if (!has_bytes(pos, size, 23)) { Log.warn("GX_LOAD_AURORA_TLUT read overrun"); return false; }
    const auto idx = data[pos];
    pos += 1;
    if (idx >= MaxTluts) { Log.warn("invalid tlut slot {}", idx); return false; }
    auto& slot = register_state().loadedTluts[idx];
    slot.data = reinterpret_cast<const void*>(read_u64(data + pos, bigEndian));
    pos += 8;
    slot.format = static_cast<GXTlutFmt>(read_u32(data + pos, bigEndian));
    pos += 4;
    slot.numEntries = read_u16(data + pos, bigEndian);
    pos += 2;
    slot.tlutObjId = read_u32(data + pos, bigEndian);
    pos += 4;
    slot.tlutDataVersion = read_u32(data + pos, bigEndian);
    pos += 4;
    slot.set_no_cache(false); // Reset no-cache flag
    register_state().stateDirty = true;
  } else if (subCmd == GX_LOAD_AURORA_DESTROY_TEXOBJ) {
    if (!has_bytes(pos, size, 4)) { Log.warn("GX_LOAD_AURORA_DESTROY_TEXOBJ read overrun"); return false; }
    evict_texture_object(read_u32(data + pos, bigEndian));
    pos += 4;
  } else if (subCmd == GX_LOAD_AURORA_DESTROY_TLUT) {
    if (!has_bytes(pos, size, 4)) { Log.warn("GX_LOAD_AURORA_DESTROY_TLUT read overrun"); return false; }
    evict_tlut_object(read_u32(data + pos, bigEndian));
    pos += 4;
  } else if (subCmd == GX_LOAD_AURORA_DESTROY_COPY_TEX) {
    if (!has_bytes(pos, size, 8)) { Log.warn("GX_LOAD_AURORA_DESTROY_COPY_TEX read overrun"); return false; }
    evict_copy_texture(reinterpret_cast<const void*>(read_u64(data + pos, bigEndian)));
    pos += 8;
  } else if (subCmd == GX_LOAD_AURORA_INVALIDATE_TEX_ALL) {
    invalidate_static_texture_cache();
  } else if (subCmd == GX_LOAD_AURORA_DEBUG_GROUP_PUSH) {
    std::string label;
    if (!read_string(data, pos, size, bigEndian, label)) return false;
    gfx::push_debug_group(std::move(label));
  } else if (subCmd == GX_LOAD_AURORA_DEBUG_GROUP_POP) {
    aurora_pop_debug_group();
  } else if (subCmd == GX_LOAD_AURORA_DEBUG_MARKER_INSERT) {
    std::string label;
    if (!read_string(data, pos, size, bigEndian, label)) return false;
    gfx::insert_debug_marker(std::move(label));
  }

  else {
    static u32 unknownAuroraLogCount = 0;
    if (unknownAuroraLogCount < 16) {
      Log.warn("Unknown Aurora subcommand: {:04X}; stopping FIFO decode", subCmd);
      ++unknownAuroraLogCount;
    }
    return false;
  }
  return true;
}

} // namespace aurora::gx::fifo
