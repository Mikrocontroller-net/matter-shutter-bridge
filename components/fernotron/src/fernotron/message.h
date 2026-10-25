#pragma once

#include <array>
#include <stddef.h>
#include <stdint.h>

/// Fernotron plain command messages: 5 data bytes plus a checksum.
///
/// Layout and rules follow tronferno-lib (MIT, github.com/zwiebert/tronferno-lib)
/// and FernoPy's protocol notes; they are to be confirmed bit for bit against
/// captures of the user's own 2411 in test/vectors.
///
///     byte 0..2  sender ID; the high nibble of byte 0 is the device type
///     byte 3     counter (high nibble, 1..15) | member code (low nibble)
///     byte 4     group (high nibble, 0..7)    | command (low nibble)
///     byte 5     sum of bytes 0..4, mod 256
namespace fernotron {

/// One complete message as sent on air, checksum included.
using Message = std::array<uint8_t, 6>;

/// Command codes (low nibble of byte 4).
enum class Command : uint8_t {
  Stop = 0x3,
  Up = 0x4,
  Down = 0x5,
  SunDown = 0x6,
  SunUp = 0x7,
  SunInst = 0x8,
  EndPosUp = 0x9,        ///< Runs up until STOP; that point becomes the end position.
  EndPosDown = 0xa,      ///< Same as EndPosUp, downwards.
  ToggleRotation = 0xb,  ///< Swaps the motor's up and down.
  Set = 0xd,             ///< Opens the receiver's pairing window.
  Program = 0xf,         ///< Header of timer/clock messages (not sent by this firmware).
};

/// Device type: the high nibble of the sender ID.
enum class DeviceType : uint8_t {
  PlainSender = 0x10,
  SunSensor = 0x20,
  CentralUnit = 0x80,  ///< 2411; the only type that carries group and member.
  Receiver = 0x90,     ///< Motor code: 9 followed by the code on the motor label.
};

/// Who a message is addressed to.
struct Target {
  uint32_t id;         ///< 24-bit sender ID, e.g. 0x80495d for a 2411.
  uint8_t group = 0;   ///< 1..7; 0 addresses all groups (central unit only).
  uint8_t member = 0;  ///< 1..7; 0 addresses all members (central unit only).
};

/// Why buildMessage() refused a message.
enum class Error : uint8_t { None, BadGroup, BadMember, BadCounter, UnknownDeviceType, UnsafeBroadcast };

/// Builds the message for `cmd` to `target` with the given counter.
///
/// Set, end-position and rotation commands to group or member 0 are refused
/// (Error::UnsafeBroadcast): they would reprogram every motor in reach.
/// @param counter 1..15; all repeats of one command share it.
/// @return Error::None and a complete `out`, or the reason and `out` untouched.
Error buildMessage(const Target &target, Command cmd, uint8_t counter, Message &out);

/// Sum of `len` bytes, mod 256.
uint8_t checksum(const uint8_t *data, size_t len);

/// Counter for the next new command: 1..15, wrapping past 0.
uint8_t nextCounter(uint8_t counter);

}  // namespace fernotron
