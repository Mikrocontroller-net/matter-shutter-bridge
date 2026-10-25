#pragma once

// Matter SDK overrides, included through CONFIG_CHIP_PROJECT_CONFIG.

/// Vendor and product name shown by the apps under the bridge's device info.
/// Vendor and product IDs stay the test IDs (0xFFF1 / 0x8000) because they
/// must match the test DAC.
#define CHIP_DEVICE_CONFIG_DEVICE_VENDOR_NAME "matter-shutter-bridge"
#define CHIP_DEVICE_CONFIG_DEVICE_PRODUCT_NAME "Shutter Bridge"

/// Retry schedule for re-establishing subscriptions after a restart: 10 s,
/// then 40 s, 40 s, 70 s, ... (10 s + Fibonacci steps of 30 s).
///
/// After a restart the bridge re-establishes the controllers' subscriptions
/// itself, so they drop sessions from before the restart; until then their
/// commands are discarded. The first attempt usually comes before Wi-Fi is
/// fully up, and the SDK default waits 10 minutes before the next one.
#define CHIP_CONFIG_SUBSCRIPTION_TIMEOUT_RESUMPTION_MIN_RETRY_INTERVAL_SECS 10
#define CHIP_CONFIG_SUBSCRIPTION_TIMEOUT_RESUMPTION_WAIT_TIME_MULTIPLIER_SECS 30
