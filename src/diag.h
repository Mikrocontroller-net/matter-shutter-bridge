#pragma once

#include <stdint.h>

// Ring buffer of received Matter commands, the main evidence for what Alexa
// actually sends. Every entry is also printed to serial as it is recorded.
//
// The Arduino Matter callbacks do not expose the accessing fabric, so entries
// carry no fabric index yet.
namespace diag {

enum class Command : uint8_t { UpOrOpen, DownOrClose, GoToLift, StopMotion };

struct Entry {
  uint32_t ms;           // millis() when the worker drained the event
  uint16_t endpoint;     // Matter endpoint ID
  Command command;       // as inferred by the Arduino library
  uint16_t target100ths; // raw TargetPositionLiftPercent100ths
  uint16_t current100ths;// CurrentPositionLiftPercent100ths at that moment
};

void record(const Entry &e);
void dump();
void clear();
const char *name(Command c);

}  // namespace diag
