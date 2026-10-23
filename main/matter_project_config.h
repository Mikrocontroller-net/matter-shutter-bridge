#pragma once

// Matter SDK overrides, included through CONFIG_CHIP_PROJECT_CONFIG.
// Shown by Alexa under the bridge's device info. Vendor and product IDs stay
// the test IDs (0xFFF1 / 0x8000) because they must match the test DAC.
#define CHIP_DEVICE_CONFIG_DEVICE_VENDOR_NAME "Fernomatter"
#define CHIP_DEVICE_CONFIG_DEVICE_PRODUCT_NAME "Fernotron Bridge"
