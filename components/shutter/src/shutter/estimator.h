#pragma once

#include <stdint.h>

namespace shutter {

/// Fully open, in hundredths of a percent closed (the Matter convention).
constexpr uint16_t OPEN = 0;
/// Fully closed, in hundredths of a percent closed.
constexpr uint16_t CLOSED = 10000;

/// Physical direction of travel.
enum class Direction : uint8_t { Up, Down };

/// Full travel times measured with a stopwatch, per direction.
struct TravelTimes {
  uint32_t upMs;    ///< Closed to open.
  uint32_t downMs;  ///< Open to closed.
};

/// Dead-reckons a shutter's position from when it started moving and its
/// travel times. The radio is one-way, so this estimate is all there is.
///
/// Times are millis()-style counters; differences survive wraparound.
class Estimator {
 public:
  /// Starts stopped at `position` (OPEN..CLOSED).
  Estimator(TravelTimes times, uint16_t position);

  /// Starts moving from wherever the estimate is at `now`.
  void start(Direction dir, uint32_t now);
  /// Stops and freezes the estimate at `now`.
  void stop(uint32_t now);
  /// Stops and sets the position outright, e.g. on reaching a known target.
  void stopAt(uint16_t position);
  /// Switches to new travel times; a running move continues from the estimate
  /// at `now` at the new rate.
  void setTimes(TravelTimes times, uint32_t now);

  /// Estimated position at `now`, clamped to OPEN..CLOSED.
  uint16_t position(uint32_t now) const;
  /// True from start() until stop() or stopAt().
  bool moving() const { return moving_; }
  /// Direction of the current or last move.
  Direction direction() const { return dir_; }

  /// Time to travel from `from` to `to`, at the speed of the direction that
  /// implies.
  uint32_t durationMs(uint16_t from, uint16_t to) const;

 private:
  TravelTimes times_;
  uint16_t startPos_;
  uint32_t startMs_ = 0;
  Direction dir_ = Direction::Up;
  bool moving_ = false;
};

}  // namespace shutter
