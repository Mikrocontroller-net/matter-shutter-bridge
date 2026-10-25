#pragma once

#include <array>
#include <stddef.h>
#include <stdint.h>

/// The access token for remote development (OTA, console, log over Wi-Fi):
/// 128 random bits as 32 lowercase hex digits, drawn once per device and
/// shown only on the USB console.
namespace remote {

/// Length of a token in hex digits.
constexpr size_t TOKEN_CHARS = 32;
/// A token as a NUL-terminated string.
using Token = std::array<char, TOKEN_CHARS + 1>;

/// Draws a token from `random`, a callable returning uniformly random 32-bit
/// words.
template <typename Random>
Token makeToken(Random &&random) {
  static constexpr char HEX[] = "0123456789abcdef";
  Token t{};
  for (size_t i = 0; i < TOKEN_CHARS; i += 8) {
    const uint32_t word = random();
    for (size_t d = 0; d < 8; d++) t[i + d] = HEX[(word >> (4 * d)) & 0xf];
  }
  t[TOKEN_CHARS] = '\0';
  return t;
}

/// True for exactly TOKEN_CHARS lowercase hex digits.
bool isValidToken(const char *s);

/// Checks an HTTP Authorization header against `token`: it must read
/// "Bearer <token>". Compares in constant time, so response timing does not
/// reveal how much of a guess was right.
bool authorized(const char *header, const char *token);

}  // namespace remote
