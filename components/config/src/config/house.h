#pragma once

#include <array>
#include <stddef.h>
#include <stdint.h>

/// The device configuration: the 2411 sender ID, radio settings and one entry
/// per shutter. Stored on the device as one versioned blob and edited over the
/// serial console, so nothing here needs a rebuild to change.
///
/// Shutters keep their slot for life: slot i is always created as the i-th
/// cover endpoint, which keeps Matter endpoint IDs stable across reboots.
namespace config {

constexpr size_t MAX_SHUTTERS = 16;     ///< Slots in a House.
constexpr size_t MAX_NAME_BYTES = 32;   ///< UTF-8 bytes; the Matter NodeLabel limit.
constexpr uint32_t MIN_TRAVEL_MS = 1000;
constexpr uint32_t MAX_TRAVEL_MS = 300000;
constexpr uint8_t MAX_REPEATS = 8;      ///< Upper bound for House::repeats.

/// One shutter: its name, its 2411 address and its travel times.
struct Shutter {
  char name[MAX_NAME_BYTES + 1] = {};  ///< NUL-terminated; shown in the apps.
  uint8_t group = 1;   ///< 1..7; a shutter never uses 0 (= all groups).
  uint8_t member = 1;  ///< 1..7.
  uint32_t upMs = 20000;    ///< Full travel up, MIN_TRAVEL_MS..MAX_TRAVEL_MS.
  uint32_t downMs = 18000;  ///< Full travel down, same range.
  bool invertEnds = false;  ///< Swap open and close (Alexa polarity quirk).
  bool invertMid = false;   ///< Mirror percentage targets.
};

/// The whole configuration.
struct House {
  uint32_t centralId = 0x800000;  ///< 2411 sender ID including the 0x80 type prefix.
  uint8_t repeats = 4;            ///< Transmissions per command, 1..MAX_REPEATS.
  uint8_t count = 0;              ///< Shutters in use; slots 0..count-1.
  std::array<Shutter, MAX_SHUTTERS> shutters{};
};

/// Why a change or a stored blob was rejected.
enum class Error : uint8_t {
  None,
  BadIndex,
  BadName,
  BadGroup,
  BadMember,
  DuplicateAddress,
  BadTimes,
  BadCentralId,
  BadRepeats,
  BadBlob,
};

/// Human-readable text for the console.
const char *describe(Error e);

/// The default house: 4 rooms as 2411 groups 1-4, two shutters each.
House defaults();

/// Checks every field and that no two shutters share an address.
Error validate(const House &h);

/// @name Setters
/// Each validates and leaves `h` unchanged on error. `index` is 0-based.
/// @{
Error setName(House &h, size_t index, const char *name);
Error setAddress(House &h, size_t index, uint8_t group, uint8_t member);
Error setTimes(House &h, size_t index, uint32_t upMs, uint32_t downMs);
Error setInvert(House &h, size_t index, bool mid, bool on);
Error setCentralId(House &h, uint32_t id);
Error setRepeats(House &h, uint8_t repeats);
/// @}

/// Largest blob serialize() writes.
constexpr size_t MAX_BLOB = 8 + MAX_SHUTTERS * (MAX_NAME_BYTES + 1 + 2 + 8 + 1);
/// Writes `h` as a versioned little-endian blob.
/// @return bytes written, or 0 if `cap` is too small.
size_t serialize(const House &h, uint8_t *out, size_t cap);
/// Reads a blob from serialize() and validates it; `out` is untouched on error.
Error deserialize(const uint8_t *data, size_t len, House &out);

}  // namespace config
