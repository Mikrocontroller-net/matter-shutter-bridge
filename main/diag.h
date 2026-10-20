#pragma once

#include <stdint.h>

// Ring buffer of received Matter commands: the evidence for what Alexa sends.
// Each entry is also logged as it is recorded. record() is safe to call from
// the Matter task: it copies under a spinlock and does not block.
namespace diag {

struct Entry {
  uint32_t ms;
  uint16_t endpoint;
  uint32_t cluster;
  uint32_t command;
  int32_t value;        // command payload (GoToLiftPercentage: 100ths), -1 if none
  uint8_t fabricIndex;  // 0 if the command had no accessing fabric
};

void record(const Entry &e);
void dump();
void clear();

}  // namespace diag
