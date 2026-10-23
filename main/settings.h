#pragma once

#include <config/command.h>
#include <config/house.h>

// The device configuration in NVS, shared by the console and the covers task.
// Every successful change is written to flash immediately.
namespace settings {

// Loads from NVS; falls back to config::defaults() (and stores them) when
// nothing valid is stored. Call once, after nvs_flash_init().
void load();

// Small copies of the current configuration. The whole House is ~0.7 KB,
// too much for the stacks of the console and main tasks.
size_t count();
config::Shutter shutter(size_t index);  // index < count()
struct Radio {
  uint32_t centralId;
  uint8_t repeats;
};
Radio radio();

config::Error apply(const config::Command &c);
config::Error setTimes(size_t index, uint32_t upMs, uint32_t downMs);

}  // namespace settings
