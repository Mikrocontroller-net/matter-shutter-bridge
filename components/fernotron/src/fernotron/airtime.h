#pragma once

#include <array>
#include <stdint.h>

// Duty-cycle guard for the EU 433 MHz band: at most 10 % airtime, i.e. 360 s
// per hour. Airtime is counted in one-minute bins over the last 60 minutes,
// which covers slightly more than an hour and so errs on the safe side.
namespace fernotron {

class Airtime {
 public:
  explicit Airtime(uint32_t budgetMsPerHour = 360000) : budgetMs_(budgetMsPerHour) {}

  // Books `onAirMs` at `nowMs` if it fits the budget; otherwise books nothing.
  bool tryConsume(uint32_t nowMs, uint32_t onAirMs);
  uint32_t usedMs(uint32_t nowMs);
  uint32_t budgetMs() const { return budgetMs_; }

 private:
  static constexpr uint32_t BINS = 60;
  static constexpr uint32_t BIN_MS = 60000;

  void advance(uint32_t nowMs);

  uint32_t budgetMs_;
  std::array<uint32_t, BINS> bins_{};
  uint32_t minute_ = 0;  // minute number of bins_[minute_ % BINS]
  bool started_ = false;
};

}  // namespace fernotron
