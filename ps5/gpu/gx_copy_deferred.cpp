// SPDX-License-Identifier: GPL-3.0-only
#include "gx_copy_deferred.h"
#include "gx_copy_readback.h"
#include "gx_copy_color.h"
#include "gfx/efb_ram_encoder.hpp"
#include "memory.h"
#include "gx_guest_write.h"
#include "gx_perf_stats.h"
#include "gpu_wait_service.h"
#include <cstring>
#include <stdexcept>

namespace mkw::agc {
namespace {
struct PendingColorCopy {
    uint32_t address;
    size_t bytes;
    ColorCopyHandle copy;
    static bool materialize(void* context) {
        const auto& pending = *static_cast<const PendingColorCopy*>(context);
        const auto started = gpu_clock_nanos();
        struct Account {
            uint64_t started;
            ~Account() {
                auto& s = gx_perf_stats();
                gx_perf_add(s.materializeNanos, gpu_clock_nanos() - started);
                gx_perf_add(s.materializeCount, 1);
            }
        } account{started};
        auto bytes = encode_native_color_copy(*pending.copy);
        if (bytes.size() != pending.bytes) throw std::logic_error("Changed deferred copy size");
        // Encode completely before committing. Allocation/conversion failure
        // leaves RAM unchanged and Memory keeps the copy pending for retry.
        std::memcpy(Memory::GetPointer(pending.address, bytes.size()), bytes.data(), bytes.size());
        GxGuestWrite::NotifyWrite(pending.address, static_cast<uint32_t>(bytes.size()));
        return true;
    }
};
}
size_t native_color_copy_bytes(const GxColorCopy& copy) {
    const auto& native=copy.nativeReadback?copy.nativeReadback:copy.target;
    if (!copy.target || !copy.target->data() || (!mkw_copy_color_format(copy.format) && !mkw_copy_depth_format(copy.format)) ||
        !native||!native->data()||copy.width != native->layout().width() || copy.height != native->layout().height())
        throw std::invalid_argument("Deferred readback requires an idle native-size color copy");
    size_t bytes = aurora::gfx::efb_ram::encoded_size(copy.format, copy.width, copy.height);
    if (!bytes || bytes > UINT32_MAX) throw std::invalid_argument("Invalid deferred copy byte count");
    return bytes;
}
uint64_t defer_native_color_copy(uint32_t guestAddress, ColorCopyHandle copy) {
    if (!copy) throw std::invalid_argument("Null deferred copy");
    const size_t bytes = native_color_copy_bytes(*copy);
    auto pending = std::make_shared<PendingColorCopy>(PendingColorCopy{guestAddress, bytes, std::move(copy)});
    auto token = Memory::RegisterDeferredRamRead(guestAddress, bytes, PendingColorCopy::materialize, pending);
    if (!token) throw std::invalid_argument("Copy destination is not a coherent Wii RAM range");
    return token;
}
}
