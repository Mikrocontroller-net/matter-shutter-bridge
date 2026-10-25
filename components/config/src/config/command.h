#pragma once

#include <stddef.h>
#include <stdint.h>

#include "house.h"

/// Parses the serial console's configuration commands. Shutter numbers are
/// 1-based on the console and 0-based in Command::index.
///
///     shutter                              list
///     shutter <n> name <text...>
///     shutter <n> address <group> <member>
///     shutter <n> times <up_ms> <down_ms>
///     shutter <n> invert ends|mid on|off
///     house                                show
///     house id <hex>
///     house repeats <n>
///     cal <n> up|down                      start the stopwatch
///     cal done | cal cancel
///     send <group> <member> up|down|stop   one command straight to the radio;
///                                          0 = all groups / all members
namespace config {

/// What a parsed command asks for.
enum class Op : uint8_t {
  Invalid,
  List,
  Name,
  Address,
  Times,
  Invert,
  ShowHouse,
  CentralId,
  Repeats,
  CalStart,
  CalDone,
  CalCancel,
  Send,
};

/// Direction for `send`.
enum class Move : uint8_t { Stop, Up, Down };

/// A parsed console command. Which fields are set depends on `op`.
struct Command {
  Op op = Op::Invalid;
  size_t index = 0;                    ///< Shutter slot, 0-based.
  char text[MAX_NAME_BYTES + 1] = {};  ///< name: the new name.
  uint32_t a = 0;    ///< Group, up_ms, ID or repeats.
  uint32_t b = 0;    ///< Member or down_ms.
  bool mid = false;  ///< invert: mid instead of ends.
  bool on = false;   ///< invert: on/off; cal: true = up.
  Move move = Move::Stop;       ///< send: the direction.
  const char *usage = nullptr;  ///< Set when op is Invalid: what to type instead.
};

/// Parses one console line split into words; argv[0] is the command word
/// itself ("shutter", "house", "cal" or "send").
Command parse(int argc, const char *const *argv);

/// Applies Name, Address, Times, Invert, CentralId and Repeats to `h`. Other
/// ops are not configuration changes and return Error::None without touching
/// `h`.
Error apply(House &h, const Command &c);

}  // namespace config
