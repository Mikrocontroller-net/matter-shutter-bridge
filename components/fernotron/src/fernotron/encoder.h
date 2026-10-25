#pragma once

#include <array>
#include <stddef.h>
#include <stdint.h>

#include "message.h"

/// Turns a message into carrier on/off durations in microseconds.
///
/// On air, each of the 6 bytes is sent twice as a 10-bit word (8 data bits LSB
/// first, then 2 parity bits), each word preceded by a stop bit:
///
///     [lead-in] preamble x7 | stop w0 | stop w1 | ... | stop w11 | lead-out
///
///     preamble  400 on,  400 off
///     stop      400 on, 3200 off
///     bit 1     400 on,  800 off
///     bit 0     800 on,  400 off
///
/// Timings are tronferno's transmitter values, which match its measurements
/// of a 2411. FernoPy's captures show 6 preamble periods instead of 7; the
/// count is configurable until our own captures settle it.
namespace fernotron {

/// One carrier-on period followed by one carrier-off period. Maps 1:1 onto an
/// RMT symbol.
struct Mark {
  uint32_t highUs;  ///< Carrier on.
  uint32_t lowUs;   ///< Carrier off.
};

/// On-air timings in microseconds; the defaults are tronferno's.
struct Timing {
  bool leadIn = true;  ///< Long first pulse that lets receivers settle their gain.
  uint32_t leadInHighUs = 25000;
  uint32_t leadInLowUs = 25000;
  uint8_t preambleCount = 7;
  uint32_t preambleHighUs = 400;
  uint32_t preambleLowUs = 400;
  uint32_t stopHighUs = 400;
  uint32_t stopLowUs = 3200;
  uint32_t shortUs = 400;
  uint32_t longUs = 800;
  uint32_t leadOutHighUs = 400;
  uint32_t leadOutLowUs = 42800;  ///< Also the gap before the next frame.
};

constexpr size_t WORD_COUNT = 12;   ///< Words per message: each byte twice.
constexpr size_t WORD_BITS = 10;    ///< 8 data bits and 2 parity bits.
constexpr size_t MAX_PREAMBLE = 12;  ///< Upper bound for Timing::preambleCount.
/// Marks in the longest frame: lead-in, preamble, stop bit and bits per word,
/// lead-out.
constexpr size_t MAX_MARKS = 1 + MAX_PREAMBLE + WORD_COUNT * (1 + WORD_BITS) + 1;

/// A complete frame as a sequence of marks.
struct Frame {
  std::array<Mark, MAX_MARKS> marks;
  size_t count = 0;  ///< Marks in use.

  /// Total duration, trailing gap included.
  uint32_t durationUs() const;
  /// Duration up to the end of the last pulse, i.e. without the trailing gap.
  /// Counted as airtime: the off periods inside a frame are part of it.
  uint32_t onAirUs() const;
};

/// 10-bit word for byte `index / 2` of a message: the byte plus 2 parity bits
/// that depend on whether it is the first (even index) or second copy.
uint16_t parityWord(uint8_t byte, unsigned index);

/// Encodes `msg` into the marks of one frame.
Frame encode(const Message &msg, const Timing &timing = Timing{});

}  // namespace fernotron
