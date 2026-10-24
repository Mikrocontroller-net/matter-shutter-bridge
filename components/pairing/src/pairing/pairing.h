#pragma once

#include <array>
#include <stddef.h>
#include <stdint.h>

// The bridge's Matter setup code: a passcode and discriminator drawn at random
// once per device instead of the SDK's public test values, and the salt for
// the SPAKE2+ verifier derived from them. Stored, so the code printed on the
// console stays valid for the life of the device.
namespace pairing {

constexpr uint32_t MAX_PASSCODE = 99999998;
constexpr uint16_t MAX_DISCRIMINATOR = 0x0FFF;  // 12 bits
constexpr size_t SALT_BYTES = 32;
// PBKDF2 rounds for the verifier: the spec's minimum, which keeps deriving it
// on the device fast.
constexpr uint32_t ITERATIONS = 1000;
constexpr size_t BLOB_BYTES = 1 + 4 + 2 + SALT_BYTES;

struct Code {
  uint32_t passcode = 0;
  uint16_t discriminator = 0;
  std::array<uint8_t, SALT_BYTES> salt{};
};

// Matter's passcode rules: 1..99999998, excluding trivial values such as
// 11111111 or 12345678.
bool isValidPasscode(uint32_t passcode);

// Draws a code from `random`, a source of uniformly random 32-bit words.
template <typename Random>
Code generate(Random &&random) {
  Code c;
  do {
    c.passcode = random() % MAX_PASSCODE + 1;
  } while (!isValidPasscode(c.passcode));
  c.discriminator = (uint16_t)(random() & MAX_DISCRIMINATOR);
  for (size_t i = 0; i < SALT_BYTES; i += 4) {
    const uint32_t word = random();
    for (size_t b = 0; b < 4 && i + b < SALT_BYTES; b++) c.salt[i + b] = (uint8_t)(word >> (8 * b));
  }
  return c;
}

// Versioned storage format. serialize() returns the bytes written (BLOB_BYTES)
// or 0 if `outLen` is too small; deserialize() rejects anything malformed.
size_t serialize(const Code &c, uint8_t *out, size_t outLen);
bool deserialize(const uint8_t *in, size_t len, Code &out);

}  // namespace pairing
