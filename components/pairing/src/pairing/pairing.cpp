#include "pairing.h"

namespace pairing {

static constexpr uint8_t BLOB_VERSION = 1;

bool isValidPasscode(uint32_t passcode) {
  if (passcode == 0 || passcode > MAX_PASSCODE) return false;
  switch (passcode) {
    case 11111111:
    case 22222222:
    case 33333333:
    case 44444444:
    case 55555555:
    case 66666666:
    case 77777777:
    case 88888888:
    case 12345678:
    case 87654321: return false;
    default: return true;
  }
}

size_t serialize(const Code &c, uint8_t *out, size_t outLen) {
  if (outLen < BLOB_BYTES) return 0;
  size_t i = 0;
  out[i++] = BLOB_VERSION;
  for (int b = 0; b < 4; b++) out[i++] = (uint8_t)(c.passcode >> (8 * b));
  out[i++] = (uint8_t)c.discriminator;
  out[i++] = (uint8_t)(c.discriminator >> 8);
  for (uint8_t s : c.salt) out[i++] = s;
  return i;
}

bool deserialize(const uint8_t *in, size_t len, Code &out) {
  if (len != BLOB_BYTES || in[0] != BLOB_VERSION) return false;
  Code c;
  size_t i = 1;
  for (int b = 0; b < 4; b++) c.passcode |= (uint32_t)in[i++] << (8 * b);
  c.discriminator = (uint16_t)(in[i] | in[i + 1] << 8);
  i += 2;
  for (uint8_t &s : c.salt) s = in[i++];
  if (!isValidPasscode(c.passcode) || c.discriminator > MAX_DISCRIMINATOR) return false;
  out = c;
  return true;
}

}  // namespace pairing
