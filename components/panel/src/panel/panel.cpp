#include "panel.h"

namespace panel {

// The WS2812 is bright behind the diffuser; these stay well below full scale.
static constexpr Color PRESSED{16, 16, 16};
static constexpr Color PAIRING_HOLD{0, 0, 64};
static constexpr Color RESET_HOLD{64, 0, 0};
static constexpr Color FRAME{48, 48, 48};
static constexpr Color NO_WIFI{48, 16, 0};
static constexpr Color BUDGET_LOW{32, 0, 0};
static constexpr Color IDLE{0, 3, 0};
static constexpr uint8_t PULSE_MIN = 2;
static constexpr uint8_t PULSE_MAX = 48;
static constexpr uint32_t RESET_BLINK_MS = 250;

Action Button::update(bool pressed, uint32_t nowMs) {
  if (pressed) {
    if (!down_) {
      down_ = true;
      pressedAtMs_ = nowMs;
    }
    lastPressedMs_ = nowMs;
    return Action::None;
  }
  if (!down_ || nowMs - lastPressedMs_ < RELEASE_DEBOUNCE_MS) return Action::None;

  down_ = false;
  const uint32_t held = lastPressedMs_ - pressedAtMs_;
  if (held >= RESET_HOLD_MS) return Action::FactoryReset;
  if (held >= PAIRING_HOLD_MS) return Action::OpenPairing;
  return Action::None;
}

Hold Button::hold() const {
  if (!down_) return Hold::None;
  const uint32_t held = lastPressedMs_ - pressedAtMs_;
  if (held >= RESET_HOLD_MS) return Hold::Reset;
  if (held >= PAIRING_HOLD_MS) return Hold::Pairing;
  return Hold::Short;
}

// Triangle wave between PULSE_MIN and PULSE_MAX over PULSE_PERIOD_MS.
static uint8_t pulse(uint32_t nowMs) {
  const uint32_t half = PULSE_PERIOD_MS / 2;
  const uint32_t phase = nowMs % PULSE_PERIOD_MS;
  const uint32_t rise = phase < half ? phase : PULSE_PERIOD_MS - phase;
  return (uint8_t)(PULSE_MIN + rise * (PULSE_MAX - PULSE_MIN) / half);
}

Color color(const Status &s, Hold hold, uint32_t nowMs) {
  switch (hold) {
    case Hold::Reset: return (nowMs / RESET_BLINK_MS) % 2 == 0 ? RESET_HOLD : Color{};
    case Hold::Pairing: return PAIRING_HOLD;
    case Hold::Short: return PRESSED;
    case Hold::None: break;
  }
  if (s.anyFrame && nowMs - s.lastFrameMs < FRAME_FLASH_MS) return FRAME;
  if (s.pairingOpen) return Color{0, 0, pulse(nowMs)};
  if (!s.wifiConnected) return NO_WIFI;
  if ((uint64_t)s.airtimeUsedMs * 5 >= (uint64_t)s.airtimeBudgetMs * 4) return BUDGET_LOW;
  return IDLE;
}

}  // namespace panel
