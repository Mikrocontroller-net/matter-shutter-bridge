#pragma once

#include <config/command.h>
#include <config/house.h>
#include <pairing/pairing.h>
#include <remote/token.h>

/// The device configuration in NVS, shared by the console and the covers task.
/// Every function is safe to call from any task; every successful change is
/// written to flash immediately.
namespace settings {

/// Loads from NVS; falls back to config::defaults() (and stores them) when
/// nothing valid is stored. Call once, after nvs_flash_init() and before any
/// other function here.
void load();

/// @name Small copies of the current configuration
/// The whole House is ~0.7 KB, too much for the stacks of the console and
/// main tasks.
/// @{
/// Shutters in use.
size_t count();
/// Shutter `index`; requires index < count().
config::Shutter shutter(size_t index);
/// The settings rf_tx needs.
struct Radio {
  uint32_t centralId;  ///< 2411 sender ID.
  uint8_t repeats;     ///< Transmissions per command.
};
/// The current radio settings.
Radio radio();
/// @}

/// @name Last known shutter positions
/// Matter convention, 0 = open. Kept apart from the configuration because
/// they change with every move.
/// @{
/// Saved position of shutter `index`; OPEN when nothing is stored.
uint16_t savedPosition(size_t index);
/// Stores the position of shutter `index`; call when a move ends.
void savePosition(size_t index, uint16_t position);
/// @}

/// @name The device's Matter setup code
/// A factory reset of the Matter pairing leaves it in place, so the code
/// printed once stays valid.
/// @{
/// @return false when no valid code is stored.
bool loadSetupCode(pairing::Code &out);
void saveSetupCode(const pairing::Code &code);
/// @}

/// @name The access token for remote development (see main/remote.h)
/// Kept, like the setup code, across a factory reset of the Matter pairing.
/// @{
/// @return false when no valid token is stored.
bool loadRemoteToken(remote::Token &out);
void saveRemoteToken(const remote::Token &token);
/// @}

/// Applies a parsed console command (see config::apply) and stores the result.
config::Error apply(const config::Command &c);
/// Sets measured travel times for shutter `index` and stores them.
config::Error setTimes(size_t index, uint32_t upMs, uint32_t downMs);

}  // namespace settings
