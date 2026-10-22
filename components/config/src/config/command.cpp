#include "command.h"

#include <stdlib.h>
#include <string.h>

namespace config {

static const char *const SHUTTER_USAGE =
    "shutter | shutter <n> name <text> | address <group> <member> | times <up_ms> <down_ms> | "
    "invert ends|mid on|off";
static const char *const HOUSE_USAGE = "house | house id <hex> | house repeats <n>";
static const char *const CAL_USAGE = "cal <n> up|down | cal done | cal cancel";

static bool parseNumber(const char *s, int base, uint32_t &out) {
  if (!s || !*s) return false;
  char *end = nullptr;
  const unsigned long v = strtoul(s, &end, base);
  if (*end != '\0' || v > 0xffffffffUL) return false;
  out = (uint32_t)v;
  return true;
}

static bool parseIndex(const char *s, size_t &index) {
  uint32_t n = 0;
  if (!parseNumber(s, 10, n) || n < 1 || n > MAX_SHUTTERS) return false;
  index = n - 1;
  return true;
}

static bool parseOnOff(const char *s, bool &on) {
  if (strcmp(s, "on") == 0) on = true;
  else if (strcmp(s, "off") == 0) on = false;
  else return false;
  return true;
}

static Command invalid(const char *usage) {
  Command c;
  c.usage = usage;
  return c;
}

static Command parseShutter(int argc, const char *const *argv) {
  Command c;
  if (argc == 1) {
    c.op = Op::List;
    return c;
  }
  if (argc < 4 || !parseIndex(argv[1], c.index)) return invalid(SHUTTER_USAGE);
  const char *what = argv[2];

  if (strcmp(what, "name") == 0) {
    // The console splits on spaces; join the rest back into one name.
    size_t len = 0;
    for (int i = 3; i < argc; i++) {
      const size_t part = strlen(argv[i]);
      const size_t sep = (i > 3) ? 1 : 0;
      if (len + sep + part > MAX_NAME_BYTES) return invalid("name is longer than 32 bytes");
      if (sep) c.text[len++] = ' ';
      memcpy(c.text + len, argv[i], part);
      len += part;
    }
    c.op = Op::Name;
  } else if (strcmp(what, "address") == 0 && argc == 5) {
    if (!parseNumber(argv[3], 10, c.a) || !parseNumber(argv[4], 10, c.b)) return invalid(SHUTTER_USAGE);
    c.op = Op::Address;
  } else if (strcmp(what, "times") == 0 && argc == 5) {
    if (!parseNumber(argv[3], 10, c.a) || !parseNumber(argv[4], 10, c.b)) return invalid(SHUTTER_USAGE);
    c.op = Op::Times;
  } else if (strcmp(what, "invert") == 0 && argc == 5) {
    if (strcmp(argv[3], "mid") == 0) c.mid = true;
    else if (strcmp(argv[3], "ends") != 0) return invalid(SHUTTER_USAGE);
    if (!parseOnOff(argv[4], c.on)) return invalid(SHUTTER_USAGE);
    c.op = Op::Invert;
  } else {
    return invalid(SHUTTER_USAGE);
  }
  return c;
}

static Command parseHouse(int argc, const char *const *argv) {
  Command c;
  if (argc == 1) {
    c.op = Op::ShowHouse;
    return c;
  }
  if (argc != 3) return invalid(HOUSE_USAGE);
  if (strcmp(argv[1], "id") == 0) {
    const char *hex = argv[2];
    if (hex[0] == '0' && (hex[1] == 'x' || hex[1] == 'X')) hex += 2;
    if (!parseNumber(hex, 16, c.a)) return invalid(HOUSE_USAGE);
    c.op = Op::CentralId;
  } else if (strcmp(argv[1], "repeats") == 0) {
    if (!parseNumber(argv[2], 10, c.a)) return invalid(HOUSE_USAGE);
    c.op = Op::Repeats;
  } else {
    return invalid(HOUSE_USAGE);
  }
  return c;
}

static Command parseCal(int argc, const char *const *argv) {
  Command c;
  if (argc == 2 && strcmp(argv[1], "done") == 0) {
    c.op = Op::CalDone;
  } else if (argc == 2 && strcmp(argv[1], "cancel") == 0) {
    c.op = Op::CalCancel;
  } else if (argc == 3 && parseIndex(argv[1], c.index)) {
    if (strcmp(argv[2], "up") == 0) c.on = true;
    else if (strcmp(argv[2], "down") != 0) return invalid(CAL_USAGE);
    c.op = Op::CalStart;
  } else {
    return invalid(CAL_USAGE);
  }
  return c;
}

Command parse(int argc, const char *const *argv) {
  if (argc < 1) return invalid(SHUTTER_USAGE);
  if (strcmp(argv[0], "shutter") == 0) return parseShutter(argc, argv);
  if (strcmp(argv[0], "house") == 0) return parseHouse(argc, argv);
  if (strcmp(argv[0], "cal") == 0) return parseCal(argc, argv);
  return invalid(SHUTTER_USAGE);
}

Error apply(House &h, const Command &c) {
  switch (c.op) {
    case Op::Name: return setName(h, c.index, c.text);
    case Op::Address:
      if (c.a > 7) return Error::BadGroup;
      if (c.b > 7) return Error::BadMember;
      return setAddress(h, c.index, (uint8_t)c.a, (uint8_t)c.b);
    case Op::Times: return setTimes(h, c.index, c.a, c.b);
    case Op::Invert: return setInvert(h, c.index, c.mid, c.on);
    case Op::CentralId: return setCentralId(h, c.a);
    case Op::Repeats:
      if (c.a > MAX_REPEATS) return Error::BadRepeats;
      return setRepeats(h, (uint8_t)c.a);
    default: return Error::None;
  }
}

}  // namespace config
