#pragma once

// The Matter setup code (QR code and 11-digit code) for pairing. Each device
// draws its own at first boot and keeps it in NVS; the SDK's public test code
// is not used.
namespace setup_code {

// Loads or generates the code and hands it to esp-matter. Call after
// settings::load() and before esp_matter::start().
void init();

// Prints the 11-digit code, the QR code payload and a link that renders it.
void print();

}  // namespace setup_code
