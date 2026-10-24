#pragma once

#include <stdint.h>

// The stick's front panel: one RGB LED under one button. Decides what the LED
// shows and what holding the button does; the firmware samples the button and
// drives the LED.
namespace panel {

struct Color {
  uint8_t r = 0, g = 0, b = 0;
  bool operator==(const Color &o) const { return r == o.r && g == o.g && b == o.b; }
  bool operator!=(const Color &o) const { return !(*this == o); }
};

// From PAIRING_HOLD_MS a release opens the pairing window, from RESET_HOLD_MS
// it factory-resets. Deciding on release lets the LED show beforehand what
// letting go will do.
constexpr uint32_t PAIRING_HOLD_MS = 3000;
constexpr uint32_t RESET_HOLD_MS = 10000;
// A release counts once the button has read released this long (contact bounce).
constexpr uint32_t RELEASE_DEBOUNCE_MS = 50;

// What releasing the button now would do.
enum class Hold : uint8_t { None, Short, Pairing, Reset };
enum class Action : uint8_t { None, OpenPairing, FactoryReset };

class Button {
 public:
  // Feed the raw button state every few tens of milliseconds.
  Action update(bool pressed, uint32_t nowMs);
  Hold hold() const;

 private:
  bool down_ = false;
  uint32_t pressedAtMs_ = 0;
  uint32_t lastPressedMs_ = 0;
};

struct Status {
  bool pairingOpen = false;
  bool wifiConnected = false;
  bool anyFrame = false;     // a radio frame has gone out since boot
  uint32_t lastFrameMs = 0;  // start of the latest radio frame
  uint32_t airtimeUsedMs = 0;
  uint32_t airtimeBudgetMs = 1;
};

// How long the LED flashes white for each radio frame.
constexpr uint32_t FRAME_FLASH_MS = 60;
// Period of the blue pulse while the pairing window is open.
constexpr uint32_t PULSE_PERIOD_MS = 2000;

// LED colour, most important first: button hold, radio frame, pairing window,
// no Wi-Fi, airtime above 80 % of the duty-cycle budget, idle (dim green).
Color color(const Status &s, Hold hold, uint32_t nowMs);

}  // namespace panel
