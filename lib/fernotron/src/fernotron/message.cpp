#include "message.h"

namespace fernotron {

static constexpr uint8_t MEMBER_FROM_SUN_SENSOR = 0x1;
static constexpr uint8_t MEMBER_FROM_PLAIN_SENDER = 0x2;
static constexpr uint8_t MEMBER_TO_RECEIVER = 0x7;
static constexpr uint8_t MEMBER_OFFSET = 7;  // member n is sent as n + 7

static bool isSetupCommand(Command cmd) {
  return cmd == Command::Set || cmd == Command::EndPosUp || cmd == Command::EndPosDown ||
         cmd == Command::ToggleRotation;
}

uint8_t checksum(const uint8_t *data, size_t len) {
  uint8_t sum = 0;
  for (size_t i = 0; i < len; i++) sum = (uint8_t)(sum + data[i]);
  return sum;
}

uint8_t nextCounter(uint8_t counter) {
  const uint8_t next = (uint8_t)((counter + 1) & 0x0f);
  return next == 0 ? 1 : next;
}

Error buildMessage(const Target &target, Command cmd, uint8_t counter, Message &out) {
  if (counter < 1 || counter > 15) return Error::BadCounter;

  const uint8_t type = (uint8_t)((target.id >> 16) & 0xf0);
  uint8_t member = 0;
  uint8_t group = 0;
  switch ((DeviceType)type) {
    case DeviceType::CentralUnit:
      if (target.group > 7) return Error::BadGroup;
      if (target.member > 7) return Error::BadMember;
      if (isSetupCommand(cmd) && (target.group == 0 || target.member == 0)) return Error::UnsafeBroadcast;
      group = target.group;
      member = target.member == 0 ? 0 : (uint8_t)(target.member + MEMBER_OFFSET);
      break;
    case DeviceType::Receiver: member = MEMBER_TO_RECEIVER; break;
    case DeviceType::PlainSender: member = MEMBER_FROM_PLAIN_SENDER; break;
    case DeviceType::SunSensor: member = MEMBER_FROM_SUN_SENSOR; break;
    default: return Error::UnknownDeviceType;
  }

  out[0] = (uint8_t)(target.id >> 16);
  out[1] = (uint8_t)(target.id >> 8);
  out[2] = (uint8_t)target.id;
  out[3] = (uint8_t)((counter << 4) | member);
  out[4] = (uint8_t)((group << 4) | ((uint8_t)cmd & 0x0f));
  out[5] = checksum(out.data(), 5);
  return Error::None;
}

}  // namespace fernotron
