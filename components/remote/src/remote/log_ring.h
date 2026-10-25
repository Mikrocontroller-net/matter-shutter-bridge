#pragma once

#include <array>
#include <stddef.h>
#include <stdint.h>

namespace remote {

/// Keeps the last `N` bytes of log output, addressed by absolute offsets, so a
/// reader can poll for what is new since its last read and see when it fell
/// behind. Not thread-safe; the caller serialises access.
template <size_t N>
class LogRing {
 public:
  /// Appends `len` bytes, dropping the oldest when full.
  void append(const char *data, size_t len) {
    for (size_t i = 0; i < len; i++) buf_[(end_ + i) % N] = data[i];
    end_ += len;
  }

  /// Offset just past the newest byte: the total ever appended.
  uint64_t end() const { return end_; }

  /// Offset of the oldest byte still kept.
  uint64_t begin() const { return end_ > N ? end_ - N : 0; }

  /// Copies up to `cap` bytes starting at offset `from` into `out`. If `from`
  /// is older than begin(), reading starts at begin() instead and `from` is
  /// updated to say so.
  /// @return bytes copied; `from` + the result is where the next read starts.
  size_t read(uint64_t &from, char *out, size_t cap) const {
    if (from < begin()) from = begin();
    if (from >= end_) return 0;
    const uint64_t available = end_ - from;
    const size_t n = available < cap ? (size_t)available : cap;
    for (size_t i = 0; i < n; i++) out[i] = buf_[(from + i) % N];
    return n;
  }

 private:
  std::array<char, N> buf_{};
  uint64_t end_ = 0;
};

}  // namespace remote
