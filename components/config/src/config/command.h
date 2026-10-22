#pragma once

#include <stddef.h>
#include <stdint.h>

#include "house.h"

// Parses the serial console's configuration commands. Shutter numbers are
// 1-based on the console and 0-based in `index`.
//
//   shutter                              list
//   shutter <n> name <text...>
//   shutter <n> address <group> <member>
//   shutter <n> times <up_ms> <down_ms>
//   shutter <n> invert ends|mid on|off
//   house                                show
//   house id <hex>
//   house repeats <n>
//   cal <n> up|down                      start the stopwatch
//   cal done | cal cancel
namespace config {

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
};

struct Command {
  Op op = Op::Invalid;
  size_t index = 0;
  char text[MAX_NAME_BYTES + 1] = {};
  uint32_t a = 0;    // group, up_ms, ID, repeats
  uint32_t b = 0;    // member, down_ms
  bool mid = false;  // invert: mid instead of ends
  bool on = false;   // invert: on/off; cal: true = up
  const char *usage = nullptr;  // set when op is Invalid
};

// argv[0] is the command word itself ("shutter", "house" or "cal").
Command parse(int argc, const char *const *argv);

// Applies Name, Address, Times, Invert, CentralId and Repeats. Other ops are
// not configuration changes and return Error::None without touching `h`.
Error apply(House &h, const Command &c);

}  // namespace config
