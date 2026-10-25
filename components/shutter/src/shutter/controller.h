#pragma once

#include <stdint.h>

#include "estimator.h"

namespace shutter {

/// What to transmit to the shutter right now.
enum class Action : uint8_t { None, Up, Down, Stop };

/// Mid targets closer than this to the current estimate are treated as
/// already reached (0.5 %).
constexpr uint16_t DEADBAND = 50;

/// Runtime switches for Alexa's polarity quirks. Ends are targets 0 and 10000
/// (open/close); mid is everything between. Each mirrors its targets.
struct Flags {
  bool invertEnds = false;
  bool invertMid = false;
};

/// Per-shutter settings for a Controller.
struct Config {
  TravelTimes times{0, 0};
  Flags flags;
  /// Extra time past the estimated arrival at an end before the shutter is
  /// reported stopped, so the estimate resyncs against the end stop.
  uint32_t endMarginMs = 2000;
};

/// Turns Matter targets into Fernotron UP / DOWN / STOP actions for one
/// shutter. Mid targets are reached with a timed STOP; ends run into the
/// motor's end stop. Positions in and out use the Matter convention
/// (OPEN..CLOSED). Not thread-safe; one task owns it.
class Controller {
 public:
  /// Starts stopped at `position`, e.g. the one saved before a restart.
  Controller(const Config &cfg, uint16_t position);

  /// Handles GoToLiftPercentage, UpOrOpen (OPEN) and DownOrClose (CLOSED).
  /// Ends always transmit, since a remote may have moved the shutter.
  /// @return what to transmit now.
  Action goTo(uint16_t target, uint32_t now);
  /// Runs to an end stop in the physical direction, ignoring the polarity
  /// flags. For the console and calibration, not for Matter commands.
  Action run(Direction dir, uint32_t now);
  /// Handles StopMotion. Always transmits STOP, since a remote may have moved
  /// the shutter.
  Action stop(uint32_t now);
  /// Advances time; call often.
  /// @return a scheduled STOP when one is due, otherwise Action::None.
  Action tick(uint32_t now);
  /// Applies a new calibration. A move in progress keeps going and its
  /// scheduled STOP or arrival at the end is rescheduled for the new times.
  void setTimes(TravelTimes times, uint32_t now);

  /// Estimated position at `now`.
  uint16_t position(uint32_t now) const { return est_.position(now); }
  /// The current or last target, after the polarity flags.
  uint16_t target() const { return target_; }
  /// True until the target is reached; for ends, until the margin has passed.
  bool moving() const { return est_.moving(); }
  /// Direction of the current or last move.
  Direction direction() const { return est_.direction(); }

  /// Polarity flags; changes apply to the next goTo().
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
