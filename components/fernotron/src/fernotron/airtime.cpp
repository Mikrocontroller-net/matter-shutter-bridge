#include "airtime.h"

namespace fernotron {

void Airtime::advance(uint32_t nowMs) {
  const uint32_t minute = nowMs / BIN_MS;
  if (!started_) {
    minute_ = minute;
    started_ = true;
    return;
  }
  // A counter wrap (every ~49.7 days) looks like a jump backwards; treat it
  // like a long pause.
  const uint32_t elapsed = minute >= minute_ ? minute - minute_ : BINS;
  if (elapsed >= BINS) {
    bins_.fill(0);
  } else {
    for (uint32_t m = 1; m <= elapsed; m++) bins_[(minute_ + m) % BINS] = 0;
  }
  minute_ = minute;
}

uint32_t Airtime::usedMs(uint32_t nowMs) {
  advance(nowMs);
  uint32_t total = 0;
  for (uint32_t v : bins_) total += v;
  return total;
}

bool Airtime::tryConsume(uint32_t nowMs, uint32_t onAirMs) {
  if (usedMs(nowMs) + onAirMs > budgetMs_) return false;
  bins_[minute_ % BINS] += onAirMs;
  return true;
}

}  // namespace fernotron
