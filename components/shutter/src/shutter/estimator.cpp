#include "estimator.h"

namespace shutter {

Estimator::Estimator(TravelTimes times, uint16_t position)
    : times_(times), startPos_(position > CLOSED ? CLOSED : position) {}

void Estimator::start(Direction dir, uint32_t now) {
  startPos_ = position(now);
  startMs_ = now;
  dir_ = dir;
  moving_ = true;
}

void Estimator::stop(uint32_t now) {
  startPos_ = position(now);
  moving_ = false;
}

void Estimator::stopAt(uint16_t position) {
  startPos_ = position > CLOSED ? CLOSED : position;
  moving_ = false;
}

void Estimator::setTimes(TravelTimes times, uint32_t now) {
  if (moving_) {
    startPos_ = position(now);
    startMs_ = now;
  }
  times_ = times;
}

uint16_t Estimator::position(uint32_t now) const {
  if (!moving_) return startPos_;
  const uint32_t travel = dir_ == Direction::Up ? times_.upMs : times_.downMs;
  const uint32_t elapsed = now - startMs_;
  if (travel == 0) return dir_ == Direction::Up ? OPEN : CLOSED;

  const uint64_t moved64 = (uint64_t)elapsed * CLOSED / travel;
  const uint32_t moved = moved64 > CLOSED ? CLOSED : (uint32_t)moved64;
  if (dir_ == Direction::Up) return moved >= startPos_ ? OPEN : (uint16_t)(startPos_ - moved);
  return startPos_ + moved >= CLOSED ? CLOSED : (uint16_t)(startPos_ + moved);
}

uint32_t Estimator::durationMs(uint16_t from, uint16_t to) const {
  const bool down = to > from;
  const uint32_t distance = down ? to - from : from - to;
  const uint32_t travel = down ? times_.downMs : times_.upMs;
  return (uint32_t)((uint64_t)distance * travel / CLOSED);
}

}  // namespace shutter
