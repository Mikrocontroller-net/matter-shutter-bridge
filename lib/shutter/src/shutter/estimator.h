#pragma once

#include <stdint.h>

namespace shutter {

// Positions follow the Matter convention, in hundredths of a percent.
constexpr uint16_t OPEN = 0;
constexpr uint16_t CLOSED = 10000;

enum class Direction : uint8_t { Up, Down };

// Full travel times measured with a stopwatch, per direction.
struct TravelTimes {
  uint32_t upMs;
  uint32_t downMs;
};

// Dead-reckons a shutter's position from when it started moving and its
// travel times. The radio is one-way, so this estimate is all there is.
// Times are millis()-style counters; differences survive wraparound.
class Estimator {
 public:
  Estimator(TravelTimes times, uint16_t position);

  // Starts moving from wherever the estimate is at `now`.
  void start(Direction dir, uint32_t now);
  void stop(uint32_t now);
  // Stops and sets the position outright, e.g. on reaching a known target.
  void stopAt(uint16_t position);

  uint16_t position(uint32_t now) const;
  bool moving() const { return moving_; }
  Direction direction() const { return dir_; }

  // Time to travel between two positions in the direction that implies.
  uint32_t durationMs(uint16_t from, uint16_t to) const;

 private:
  TravelTimes times_;
  uint16_t startPos_;
  uint32_t startMs_ = 0;
  Direction dir_ = Direction::Up;
  bool moving_ = false;
};

}  // namespace shutter
