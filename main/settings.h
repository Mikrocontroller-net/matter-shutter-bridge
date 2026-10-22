#pragma once

#include <config/command.h>
#include <config/house.h>

// The device configuration in NVS, shared by the console and the covers task.
// Every successful change is written to flash immediately.
namespace settings {

// Loads from NVS; falls back to config::defaults() (and stores them) when
// nothing valid is stored. Call once, after nvs_flash_init().
void load();

// A consistent copy of the current configuration.
config::House get();

config::Error apply(const config::Command &c);
config::Error setTimes(size_t index, uint32_t upMs, uint32_t downMs);

}  // namespace settings
