#pragma once

#include <array>
#include <stddef.h>
#include <stdint.h>

// Fernotron plain command messages: 5 data bytes plus a checksum.
//
// Layout and rules follow tronferno-lib (MIT, github.com/zwiebert/tronferno-lib)
// and FernoPy's protocol notes; they are to be confirmed bit for bit against
// captures of the user's own 2411 in test/vectors.
//
//   byte 0..2  sender ID; the high nibble of byte 0 is the device type
//   byte 3     counter (high nibble, 1..15) | member code (low nibble)
//   byte 4     group (high nibble, 0..7)    | command (low nibble)
//   byte 5     sum of bytes 0..4, mod 256
namespace fernotron {

using Message = std::array<uint8_t, 6>;

enum class Command : uint8_t {
  Stop = 0x3,
  Up = 0x4,
  Down = 0x5,
  SunDown = 0x6,
  SunUp = 0x7,
  SunInst = 0x8,
  EndPosUp = 0x9,        // runs up until STOP; that point becomes the end position
  EndPosDown = 0xa,      // same, downwards
  ToggleRotation = 0xb,  // swaps the motor's up/down
  Set = 0xd,             // opens the receiver's pairing window
  Program = 0xf,         // header of timer/clock messages (not sent by this firmware)
};

// High nibble of the sender ID.
enum class DeviceType : uint8_t {
  PlainSender = 0x10,
  SunSensor = 0x20,
  CentralUnit = 0x80,  // 2411; the only type that carries group and member
  Receiver = 0x90,     // motor code: 9 followed by the code on the motor label
};

struct Target {
  uint32_t id;         // 24-bit sender ID, e.g. 0x80495d for a 2411
  uint8_t group = 0;   // 1..7, 0 = all groups (central unit only)
  uint8_t member = 0;  // 1..7, 0 = all members (central unit only)
};

enum class Error : uint8_t { None, BadGroup, BadMember, BadCounter, UnknownDeviceType, UnsafeBroadcast };

// Fills `out`. Set, end-position and rotation commands to group or member 0
// are refused: they would reprogram every motor in reach.
Error buildMessage(const Target &target, Command cmd, uint8_t counter, Message &out);

uint8_t checksum(const uint8_t *data, size_t len);

// Counter for the next new command: 1..15, wrapping past 0. Repeats of one
// command keep the same counter.
uint8_t nextCounter(uint8_t counter);

}  // namespace fernotron
