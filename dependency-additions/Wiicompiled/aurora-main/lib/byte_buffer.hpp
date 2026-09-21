#pragma once
// CPU byte storage shared by graphics backends. Moved unchanged from gfx/common.hpp.
#include "internal.hpp"
#include <cstdlib>
#include <cstring>
namespace aurora {
class ByteBuffer {
public:
  ByteBuffer() noexcept = default;
  explicit ByteBuffer(size_t size) noexcept
  : m_data(static_cast<uint8_t*>(calloc(1, size))), m_length(size), m_capacity(size) {}
  explicit ByteBuffer(uint8_t* data, size_t size) noexcept : m_data(data), m_capacity(size), m_owned(false) {}
  ~ByteBuffer() noexcept {
    if (m_data != nullptr && m_owned) {
      free(m_data);
    }
  }
  ByteBuffer(ByteBuffer&& rhs) noexcept
  : m_data(rhs.m_data), m_length(rhs.m_length), m_capacity(rhs.m_capacity), m_owned(rhs.m_owned) {
    rhs.m_data = nullptr;
    rhs.m_length = 0;
    rhs.m_capacity = 0;
    rhs.m_owned = true;
  }
  ByteBuffer& operator=(ByteBuffer&& rhs) noexcept {
    if (m_data != nullptr && m_owned) {
      free(m_data);
    }
    m_data = rhs.m_data;
    m_length = rhs.m_length;
    m_capacity = rhs.m_capacity;
    m_owned = rhs.m_owned;
    rhs.m_data = nullptr;
    rhs.m_length = 0;
    rhs.m_capacity = 0;
    rhs.m_owned = true;
    return *this;
  }
  ByteBuffer(ByteBuffer const&) = delete;
  ByteBuffer& operator=(ByteBuffer const&) = delete;
  operator ArrayRef<uint8_t>() const noexcept { return {m_data, m_length}; }

  [[nodiscard]] uint8_t* data() noexcept { return m_data; }
  [[nodiscard]] const uint8_t* data() const noexcept { return m_data; }
  [[nodiscard]] size_t size() const noexcept { return m_length; }
  [[nodiscard]] bool empty() const noexcept { return m_length == 0; }

  void append(const void* data, size_t size) {
    resize(m_length + size, false);
    memcpy(m_data + m_length, data, size);
    m_length += size;
  }

  template <typename T>
  void append(const T& obj) {
    append(&obj, sizeof(T));
  }

  void append_zeroes(size_t size) {
    resize(m_length + size, true);
    m_length += size;
  }

  // Extend the buffer without clearing the new bytes. Only for callers that overwrite the whole
  // region: the mapped staging buffers are megabytes of write-combine memory.
  void append_uninitialized(size_t size) {
    resize(m_length + size, false);
    m_length += size;
  }

  void release() {
    if (m_data != nullptr && m_owned) {
      free(m_data);
    }
    m_data = nullptr;
    m_length = 0;
    m_capacity = 0;
    m_owned = true;
  }

  void clear() {
    m_length = 0;
  }

  void reserve_extra(size_t size) { resize(m_length + size, true); }

  ByteBuffer clone() const {
    ByteBuffer clone{m_length};
    std::memcpy(clone.data(), m_data, m_length);
    return clone;
  }

private:
  uint8_t* m_data = nullptr;
  size_t m_length = 0;
  size_t m_capacity = 0;
  bool m_owned = true;

  // `size` is the total capacity needed. When `zeroed` is set, [m_length, size) has to read back as
  // zero on every branch; the early return used to leave the previous frame's bytes in the padding.
  void resize(size_t size, bool zeroed) {
    if (size == 0) {
      clear();
      return;
    }
    const size_t zeroBegin = m_length;
    if (m_data == nullptr) {
      if (zeroed) {
        m_data = static_cast<uint8_t*>(calloc(1, size));
      } else {
        m_data = static_cast<uint8_t*>(malloc(size));
      }
      m_owned = true;
      m_capacity = size;
      // calloc already cleared the whole allocation.
      return;
    }
    if (size > m_capacity) {
      if (!m_owned) {
        abort();
      }
      // Exponential expansion to avoid O(n^2) time complexity.
      size_t capacity = size;
      if (capacity < m_capacity * 2) {
        capacity = m_capacity * 2;
      }
      m_data = static_cast<uint8_t*>(realloc(m_data, capacity));
      m_capacity = capacity;
    }
    if (zeroed && size > zeroBegin) {
      memset(m_data + zeroBegin, 0, size - zeroBegin);
    }
  }
};
} // namespace aurora
