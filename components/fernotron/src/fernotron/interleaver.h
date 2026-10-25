#pragma once

#include <array>
#include <stddef.h>
#include <stdint.h>

#include "batcher.h"
#include "message.h"

namespace fernotron {

/// A command on air: its message and how many copies are still to be sent.
struct Transmission {
  Message message;       ///< Includes the counter shared by all copies.
  Batcher::Frame frame;  ///< Address and command, for overlap checks and logs.
  uint8_t copiesLeft;
};

/// Sends the copies of the commands on air in turn: one copy of each per
/// round. In a routine every room gets its first copy within about one round,
/// and each command's copies spread over several seconds, so a short burst of
/// interference rarely hits all of them. All copies of a command carry the
/// same counter, so a motor acts on the first one it receives.
/// Not thread-safe; one task owns it.
class Interleaver {
 public:
  /// Commands that can be on air at once.
  static constexpr size_t CAPACITY = 16;

  /// Queues a command's copies. Copies still pending for an overlapping
  /// address are dropped: the newer command wins, so a late copy of an UP
  /// cannot restart a shutter that a STOP just stopped. A STOP goes next;
  /// anything else joins the end of the round.
  /// @return false when full or when `t` has no copies.
  bool add(const Transmission &t);

  /// The next copy to send; `out.copiesLeft` is what remains after it.
  /// @return false when nothing is left.
  bool next(Transmission &out);

  /// True when no copies are left to send.
  bool empty() const { return count_ == 0; }

 private:
  void removeAt(size_t i);

  std::array<Transmission, CAPACITY> items_{};
  size_t count_ = 0;
  size_t cursor_ = 0;  // index of the command whose copy goes next
};

}  // namespace fernotron
