#pragma once

#include <stdint.h>

/// Ring buffer of received Matter commands: the evidence for what a controller
/// sends. Each entry is also logged as it is recorded.
namespace diag {

/// One received command.
struct Entry {
  uint32_t ms;          ///< esp_timer milliseconds when it arrived.
  uint16_t endpoint;
  uint32_t cluster;
  uint32_t command;
  int32_t value;        ///< Command payload (GoToLiftPercentage: 100ths), -1 if none.
  uint8_t fabricIndex;  ///< 0 if the command had no accessing fabric.
};

/// Stores and logs `e`, overwriting the oldest entry when full. Safe to call
/// from the Matter task: it copies under a spinlock and does not block.
void record(const Entry &e);
/// Prints all entries, oldest first.
void dump();
/// Empties the buffer.
void clear();

}  // namespace diag
