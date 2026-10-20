#include "controller.h"

namespace shutter {

static bool isEnd(uint16_t target) { return target == OPEN || target == CLOSED; }

static Action actionFor(Direction dir) { return dir == Direction::Up ? Action::Up : Action::Down; }

Controller::Controller(const Config &cfg, uint16_t position)
    : cfg_(cfg), est_(cfg.times, position), target_(est_.position(0)) {}

uint16_t Controller::applyFlags(uint16_t target) const {
  const bool invert = isEnd(target) ? cfg_.flags.invertEnds : cfg_.flags.invertMid;
  return invert ? CLOSED - target : target;
}

Action Controller::goTo(uint16_t target, uint32_t now) {
  if (target > CLOSED) target = CLOSED;
  target = applyFlags(target);
  const uint16_t current = est_.position(now);

  if (isEnd(target)) {
    const Direction dir = target == OPEN ? Direction::Up : Direction::Down;
    est_.start(dir, now);
    target_ = target;
    pending_ = Pending::EndReached;
    dueMs_ = now + est_.durationMs(current, target) + cfg_.endMarginMs;
    return actionFor(dir);
  }

  const uint16_t distance = target > current ? target - current : current - target;
  if (distance < DEADBAND) {
    if (est_.moving()) return stop(now);
    return Action::None;
  }

  const Direction dir = target > current ? Direction::Down : Direction::Up;
  est_.start(dir, now);
  target_ = target;
  pending_ = Pending::MidStop;
  dueMs_ = now + est_.durationMs(current, target);
  return actionFor(dir);
}

Action Controller::stop(uint32_t now) {
  est_.stop(now);
  target_ = est_.position(now);
  pending_ = Pending::None;
  return Action::Stop;
}

Action Controller::tick(uint32_t now) {
  if (pending_ == Pending::None || (int32_t)(now - dueMs_) < 0) return Action::None;

  const Pending done = pending_;
  pending_ = Pending::None;
  est_.stopAt(target_);
  if (done == Pending::MidStop) return Action::Stop;
  return cfg_.resendAtEnd ? (target_ == OPEN ? Action::Up : Action::Down) : Action::None;
}

}  // namespace shutter
