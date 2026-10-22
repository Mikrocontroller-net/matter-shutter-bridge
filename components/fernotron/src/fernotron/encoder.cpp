#include "encoder.h"

namespace fernotron {

static bool hasEvenOnes(uint8_t v) {
  v ^= (uint8_t)(v >> 4);
  v ^= (uint8_t)(v >> 2);
  v ^= (uint8_t)(v >> 1);
  return (v & 1) == 0;
}

uint16_t parityWord(uint8_t byte, unsigned index) {
  const bool even = hasEvenOnes(byte);
  const uint16_t parity = (index & 1) ? (even ? 3 : 1) : (even ? 0 : 2);
  return (uint16_t)(byte | (parity << 8));
}

uint32_t Frame::durationUs() const {
  uint32_t total = 0;
  for (size_t i = 0; i < count; i++) total += marks[i].highUs + marks[i].lowUs;
  return total;
}

uint32_t Frame::onAirUs() const { return count == 0 ? 0 : durationUs() - marks[count - 1].lowUs; }

Frame encode(const Message &msg, const Timing &t) {
  Frame f;
  auto put = [&f](uint32_t high, uint32_t low) { f.marks[f.count++] = {high, low}; };

  if (t.leadIn) put(t.leadInHighUs, t.leadInLowUs);
  const size_t preamble = t.preambleCount > MAX_PREAMBLE ? MAX_PREAMBLE : t.preambleCount;
  for (size_t i = 0; i < preamble; i++) put(t.preambleHighUs, t.preambleLowUs);

  for (unsigned w = 0; w < WORD_COUNT; w++) {
    put(t.stopHighUs, t.stopLowUs);
    const uint16_t word = parityWord(msg[w / 2], w);
    for (unsigned b = 0; b < WORD_BITS; b++) {
      const bool one = (word >> b) & 1;
      put(one ? t.shortUs : t.longUs, one ? t.longUs : t.shortUs);
    }
  }

  put(t.leadOutHighUs, t.leadOutLowUs);
  return f;
}

}  // namespace fernotron
