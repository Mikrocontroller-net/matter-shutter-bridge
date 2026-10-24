#pragma once

#include <esp_err.h>

// Front button (GPIO41) and the WS2812C RGB LED beneath it (GPIO35); the
// behaviour lives in components/panel. Holding the button opens the pairing
// window (3 s) or factory-resets (10 s); the LED shows status.
namespace panel_hw {

// Call after esp_matter::start(). The button works even if the LED fails.
esp_err_t start();

// From the Matter device event handler.
void setWifiConnected(bool connected);

}  // namespace panel_hw
