#include "token.h"

#include <string.h>

namespace remote {

static bool isHexDigit(char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); }

bool isValidToken(const char *s) {
  if (!s) return false;
  for (size_t i = 0; i < TOKEN_CHARS; i++) {
    if (!isHexDigit(s[i])) return false;
  }
  return s[TOKEN_CHARS] == '\0';
}

bool authorized(const char *header, const char *token) {
  static constexpr char PREFIX[] = "Bearer ";
  static constexpr size_t PREFIX_LEN = sizeof(PREFIX) - 1;
  if (!header || !isValidToken(token)) return false;
  if (strlen(header) != PREFIX_LEN + TOKEN_CHARS || strncmp(header, PREFIX, PREFIX_LEN) != 0) return false;
  uint8_t diff = 0;
  for (size_t i = 0; i < TOKEN_CHARS; i++) diff |= (uint8_t)(header[PREFIX_LEN + i] ^ token[i]);
  return diff == 0;
}

}  // namespace remote
