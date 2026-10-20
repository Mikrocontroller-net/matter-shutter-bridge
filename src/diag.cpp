#include "diag.h"

#include <Arduino.h>

namespace diag {

static constexpr size_t CAPACITY = 64;
static Entry entries[CAPACITY];
static size_t head = 0;   // next slot to write
static size_t count = 0;

const char *name(Command c) {
  switch (c) {
    case Command::UpOrOpen: return "UpOrOpen";
    case Command::DownOrClose: return "DownOrClose";
    case Command::GoToLift: return "GoToLiftPercentage";
    case Command::StopMotion: return "StopMotion";
  }
  return "?";
}

static void print(const Entry &e) {
  Serial.printf("[diag] t=%lu ep=%u cmd=%s target=%u current=%u\n", (unsigned long)e.ms, e.endpoint, name(e.command),
                e.target100ths, e.current100ths);
}

void record(const Entry &e) {
  entries[head] = e;
  head = (head + 1) % CAPACITY;
  if (count < CAPACITY) count++;
  print(e);
}

void dump() {
  Serial.printf("[diag] %u entries (oldest first)\n", (unsigned)count);
  size_t start = (head + CAPACITY - count) % CAPACITY;
  for (size_t i = 0; i < count; i++) print(entries[(start + i) % CAPACITY]);
}

void clear() {
  head = 0;
  count = 0;
}

}  // namespace diag
