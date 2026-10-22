#pragma once

// Serial console commands, under esp-matter's `matter` prefix:
//   matter shutter | matter shutter <n> name|address|times|invert ...
//   matter house | matter house id <hex> | matter house repeats <n>
//   matter cal <n> up|down | matter cal done | matter cal cancel
//   matter send <group> <member> up|down|stop
//   matter diag [clear] | matter status
namespace cli {

void registerCommands();

}  // namespace cli
