#pragma once

// Matter SDK overrides, included through CONFIG_CHIP_PROJECT_CONFIG.
// Shown by Alexa under the bridge's device info. Vendor and product IDs stay
// the test IDs (0xFFF1 / 0x8000) because they must match the test DAC.
#define CHIP_DEVICE_CONFIG_DEVICE_VENDOR_NAME "Fernomatter"
#define CHIP_DEVICE_CONFIG_DEVICE_PRODUCT_NAME "Fernotron Bridge"

// After a restart the bridge re-establishes the controllers' subscriptions
// itself, so they drop sessions from before the restart; until then their
// commands are discarded. The first attempt usually comes before Wi-Fi is
// fully up, and the SDK default waits 10 minutes before the next one. Retry
// after 10 s, then 40 s, 40 s, 70 s, ... (10 s + Fibonacci steps of 30 s).
#define CHIP_CONFIG_SUBSCRIPTION_TIMEOUT_RESUMPTION_MIN_RETRY_INTERVAL_SECS 10
#define CHIP_CONFIG_SUBSCRIPTION_TIMEOUT_RESUMPTION_WAIT_TIME_MULTIPLIER_SECS 30
