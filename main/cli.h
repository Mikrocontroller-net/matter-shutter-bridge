#pragma once

/// Serial console commands, under esp-matter's `matter esp` prefix:
///
///     matter esp shutter | matter esp shutter <n> name|address|times|invert ...
///     matter esp house | matter esp house id <hex> | matter esp house repeats <n>
///     matter esp cal <n> up|down | matter esp cal done | matter esp cal cancel
///     matter esp send <group> <member> up|down|stop
///     matter esp diag [clear] | matter esp status | matter esp pairing | matter esp remote
namespace cli {

/// Registers the commands with esp-matter's console. Call before
/// esp_matter::console::init().
void registerCommands();

}  // namespace cli
