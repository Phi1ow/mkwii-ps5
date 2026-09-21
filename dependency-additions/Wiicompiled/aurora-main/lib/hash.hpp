#pragma once
#include "internal.hpp"
#include <aurora/gfx.h>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#define XXH_STATIC_LINKING_ONLY
#include <xxhash.h>

namespace aurora {
#if INTPTR_MAX == INT32_MAX
using HashType = XXH32_hash_t;
#else
using HashType = XXH64_hash_t;
#endif
static inline HashType xxh3_hash_s(const void* input, size_t len, HashType seed = 0) {
  return static_cast<HashType>(XXH3_64bits_withSeed(input, len, seed));
}
template <typename T>
static inline HashType xxh3_hash(const T& input, HashType seed = 0) {
  // Validate that the type has no padding bytes, which can easily cause
  // hash mismatches. This also disallows floats, but that's okay for us.
  static_assert(std::has_unique_object_representations_v<T>);
  return xxh3_hash_s(&input, sizeof(T), seed);
}

// Folds two 64-bit hashes. Chaining them instead drops XXH3 onto its seeded long path past 240
// bytes, and that path regenerates a 192-byte secret on every call.
static inline HashType hash_combine(HashType lhs, HashType rhs) {
  uint64_t mixed = static_cast<uint64_t>(lhs) ^ (static_cast<uint64_t>(rhs) + 0x9E3779B97F4A7C15ull +
                                                 (static_cast<uint64_t>(lhs) << 6) + (static_cast<uint64_t>(lhs) >> 2));
  mixed ^= mixed >> 33;
  mixed *= 0xFF51AFD7ED558CCDull;
  mixed ^= mixed >> 29;
  return static_cast<HashType>(mixed);
}

// Guest-RAM write tracking hooks (see aurora_set_guest_write_hooks). A digest samples the
// generation before reading the bytes, so a racing write costs an extra digest, never a skipped one.
inline constexpr uint64_t kGuestWriteUntracked = AURORA_GUEST_WRITE_UNTRACKED;
inline AuroraGuestWriteGenerationCallback g_guestWriteGenerationHook = nullptr;
inline AuroraGuestWriteNotifyCallback g_guestWriteNotifyHook = nullptr;

inline uint64_t guest_write_generation(const void* data, size_t size) noexcept {
  if (g_guestWriteGenerationHook == nullptr || data == nullptr || size == 0) {
    return kGuestWriteUntracked;
  }
  return g_guestWriteGenerationHook(data, size);
}

inline void notify_guest_write(const void* data, size_t size) noexcept {
  if (g_guestWriteNotifyHook == nullptr || data == nullptr || size == 0) {
    return;
  }
  g_guestWriteNotifyHook(data, size);
}

// True when `stored` was taken over the same untouched bytes, so its digest still describes them.
// The untracked sentinel never matches: a source aurora cannot watch is always re-digested.
inline bool guest_write_generation_matches(uint64_t stored, uint64_t current) noexcept {
  return current != kGuestWriteUntracked && stored == current;
}

class Hasher {
public:
  explicit Hasher(const XXH64_hash_t seed = 0) {
    XXH3_INITSTATE(&state);
    XXH3_64bits_reset_withSeed(&state, seed);
  }

  void update(const void* data, const size_t size) { XXH3_64bits_update(&state, data, size); }

  template <typename T>
  void update(const T& data) {
    static_assert(std::has_unique_object_representations_v<T>);
    update(&data, sizeof(T));
  }

  [[nodiscard]] XXH64_hash_t digest() const { return XXH3_64bits_digest(&state); }

private:
  XXH3_state_t state;
};


} // namespace aurora

