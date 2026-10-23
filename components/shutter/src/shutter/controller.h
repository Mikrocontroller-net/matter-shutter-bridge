#pragma once

#include <stdint.h>

#include "estimator.h"

namespace shutter {

// What to transmit to the shutter right now.
enum class Action : uint8_t { None, Up, Down, Stop };

// Mid targets closer than this to the current estimate are treated as
// already reached (0.5 %).
constexpr uint16_t DEADBAND = 50;

// Position as reported to Matter controllers. The ends are reported one
// hundredth short of fully open/closed: the estimate can be wrong (physical
// remotes, a restart), and a controller that believes a shutter is already
// fully open or closed may skip sending "open"/"close" altogether (Alexa does).
// Shown as 0 % / 100 % all the same.
constexpr uint16_t reportedPosition(uint16_t position) {
  return position <= OPEN ? OPEN + 1 : position >= CLOSED ? CLOSED - 1 : position;
}

// Runtime switches for Alexa's polarity quirks. Ends are targets 0 and 10000
// (open/close); mid is everything between. Each mirrors its targets.
struct Flags {
  bool invertEnds = false;
  bool invertMid = false;
};

struct Config {
  TravelTimes times{0, 0};
  Flags flags;
  // Extra time past the estimated arrival at an end before the shutter is
  // reported stopped, so the estimate resyncs against the end stop.
  uint32_t endMarginMs = 2000;
  // Repeat the end command once at that point, for a shutter that missed it.
  bool resendAtEnd = true;
};

// Turns Matter targets into Fernotron UP / DOWN / STOP actions for one
// shutter. Mid targets are reached with a timed STOP; ends run into the
// motor's end stop. Positions in and out use the Matter convention.
class Controller {
 public:
  Controller(const Config &cfg, uint16_t position);

  // GoToLiftPercentage, UpOrOpen (OPEN) and DownOrClose (CLOSED).
  Action goTo(uint16_t target, uint32_t now);
  // Runs to an end stop in the physical direction, ignoring the polarity
  // flags. For the console and calibration, not for Matter commands.
  Action run(Direction dir, uint32_t now);
  // StopMotion. Always transmits STOP, since a remote may have moved it.
  Action stop(uint32_t now);
  // Call often; returns a scheduled STOP or end re-send when one is due.
  Action tick(uint32_t now);
  // New calibration. A move in progress keeps going and its scheduled STOP
  // or end re-send is rescheduled for the new times.
  void setTimes(TravelTimes times, uint32_t now);

  uint16_t position(uint32_t now) const { return est_.position(now); }
  uint16_t target() const { return target_; }
  bool moving() const { return est_.moving(); }
  Direction direction() const { return est_.direction(); }

  Flags &flags() { return cfg_.flags; }

 private:
  enum class Pending : uint8_t { None, MidStop, EndReached };

  uint16_t applyFlags(uint16_t target) const;
  Action move(uint16_t target, uint32_t now);

  Config cfg_;
  Estimator est_;
  uint16_t target_;
  Pending pending_ = Pending::None;
  uint32_t dueMs_ = 0;
};

}  // namespace shutter
