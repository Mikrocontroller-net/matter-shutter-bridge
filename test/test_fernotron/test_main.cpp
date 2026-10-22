#include <unity.h>

#include <fernotron/airtime.h>
#include <fernotron/encoder.h>
#include <fernotron/message.h>

using namespace fernotron;

// Until test/vectors holds captures of the user's own 2411, the reference is
// a central-unit header taken from tronferno-lib's tests:
// ID 0x80495d, counter 6, member 1, group 2, command Program.
static const Message TRONFERNO_HEADER = {0x80, 0x49, 0x5d, 0x68, 0x2f, 0xbd};

void setUp() {}
void tearDown() {}

static void assertMessage(const Message &expected, const Message &actual) {
  TEST_ASSERT_EQUAL_HEX8_ARRAY(expected.data(), actual.data(), expected.size());
}

// --- Message bytes -----------------------------------------------------------

static void test_checksum_is_byte_sum() {
  TEST_ASSERT_EQUAL_HEX8(0xbd, checksum(TRONFERNO_HEADER.data(), 5));
}

static void test_builds_tronferno_reference_header() {
  Message m{};
  TEST_ASSERT_EQUAL(Error::None, buildMessage({0x80495d, 2, 1}, Command::Program, 6, m));
  assertMessage(TRONFERNO_HEADER, m);
}

static void test_central_unit_group_and_member() {
  Message m{};
  TEST_ASSERT_EQUAL(Error::None, buildMessage({0x801234, 1, 2}, Command::Up, 1, m));
  assertMessage({0x80, 0x12, 0x34, 0x19, 0x14, 0xf3}, m);
}

static void test_central_unit_broadcast_is_zero_nibbles() {
  Message m{};
  TEST_ASSERT_EQUAL(Error::None, buildMessage({0x801234, 0, 0}, Command::Down, 2, m));
  TEST_ASSERT_EQUAL_HEX8(0x20, m[3]);
  TEST_ASSERT_EQUAL_HEX8(0x05, m[4]);
}

static void test_motor_code_targets_receiver() {
  // Motor label ABCDE is addressed as 0x9ABCDE; group and member are ignored.
  Message m{};
  TEST_ASSERT_EQUAL(Error::None, buildMessage({0x9abcde, 3, 4}, Command::Stop, 3, m));
  assertMessage({0x9a, 0xbc, 0xde, 0x37, 0x03, 0x6e}, m);
}

static void test_rejects_out_of_range_group_and_member() {
  Message m{};
  TEST_ASSERT_EQUAL(Error::BadGroup, buildMessage({0x801234, 8, 1}, Command::Up, 1, m));
  TEST_ASSERT_EQUAL(Error::BadMember, buildMessage({0x801234, 1, 8}, Command::Up, 1, m));
}

static void test_rejects_counter_outside_1_to_15() {
  Message m{};
  TEST_ASSERT_EQUAL(Error::BadCounter, buildMessage({0x801234, 1, 1}, Command::Up, 0, m));
  TEST_ASSERT_EQUAL(Error::BadCounter, buildMessage({0x801234, 1, 1}, Command::Up, 16, m));
}

static void test_rejects_unknown_device_type() {
  Message m{};
  TEST_ASSERT_EQUAL(Error::UnknownDeviceType, buildMessage({0x401234, 1, 1}, Command::Up, 1, m));
}

static void test_refuses_setup_commands_to_broadcast() {
  // Set and end-position commands must reach exactly one motor.
  Message m{};
  TEST_ASSERT_EQUAL(Error::UnsafeBroadcast, buildMessage({0x801234, 0, 1}, Command::Set, 1, m));
  TEST_ASSERT_EQUAL(Error::UnsafeBroadcast, buildMessage({0x801234, 1, 0}, Command::EndPosUp, 1, m));
  TEST_ASSERT_EQUAL(Error::UnsafeBroadcast, buildMessage({0x801234, 0, 0}, Command::EndPosDown, 1, m));
  TEST_ASSERT_EQUAL(Error::UnsafeBroadcast, buildMessage({0x801234, 2, 0}, Command::ToggleRotation, 1, m));
  TEST_ASSERT_EQUAL(Error::None, buildMessage({0x801234, 2, 3}, Command::Set, 1, m));
  TEST_ASSERT_EQUAL(Error::None, buildMessage({0x9abcde, 0, 0}, Command::Set, 1, m));
}

static void test_counter_wraps_and_skips_zero() {
  TEST_ASSERT_EQUAL_UINT8(2, nextCounter(1));
  TEST_ASSERT_EQUAL_UINT8(15, nextCounter(14));
  TEST_ASSERT_EQUAL_UINT8(1, nextCounter(15));
  TEST_ASSERT_EQUAL_UINT8(1, nextCounter(0));
}

// --- Word parity -------------------------------------------------------------

static void test_parity_depends_on_bit_count_and_twin_position() {
  // 0x80: odd number of ones.
  TEST_ASSERT_EQUAL_HEX16(0x280, parityWord(0x80, 0));
  TEST_ASSERT_EQUAL_HEX16(0x180, parityWord(0x80, 1));
  // 0x03: even number of ones.
  TEST_ASSERT_EQUAL_HEX16(0x003, parityWord(0x03, 0));
  TEST_ASSERT_EQUAL_HEX16(0x303, parityWord(0x03, 1));
  // Position is the word index in the message; only odd/even matters.
  TEST_ASSERT_EQUAL_HEX16(parityWord(0x5d, 1), parityWord(0x5d, 11));
}

// --- On-air pulses -----------------------------------------------------------

static Timing noLeadIn() {
  Timing t;
  t.leadIn = false;
  return t;
}

static void assertMark(uint32_t high, uint32_t low, const Mark &m) {
  TEST_ASSERT_EQUAL_UINT32(high, m.highUs);
  TEST_ASSERT_EQUAL_UINT32(low, m.lowUs);
}

static void test_frame_layout_counts() {
  // lead-in + 7 preamble + 12 words x (stop + 10 bits) + lead-out
  TEST_ASSERT_EQUAL_UINT32(1 + 7 + 12 * 11 + 1, encode(TRONFERNO_HEADER).count);
  TEST_ASSERT_EQUAL_UINT32(7 + 12 * 11 + 1, encode(TRONFERNO_HEADER, noLeadIn()).count);
}

static void test_frame_starts_with_lead_in_then_preamble_then_stop() {
  const Frame f = encode(TRONFERNO_HEADER);
  assertMark(25000, 25000, f.marks[0]);
  for (size_t i = 1; i <= 7; i++) assertMark(400, 400, f.marks[i]);
  assertMark(400, 3200, f.marks[8]);
}

static void test_first_word_bits_go_out_lsb_first_with_parity() {
  // Word 0 is 0x80 with parity 2: 0x280 = bits 0..9 = 0000000101.
  const Frame f = encode(TRONFERNO_HEADER, noLeadIn());
  const size_t first = 7 + 1;  // after preamble and stop
  for (size_t i = 0; i < 7; i++) assertMark(800, 400, f.marks[first + i]);  // 0
  assertMark(400, 800, f.marks[first + 7]);                                // 1
  assertMark(800, 400, f.marks[first + 8]);                                // parity bit 0 = 0
  assertMark(400, 800, f.marks[first + 9]);                                // parity bit 1 = 1
}

static void test_each_byte_is_sent_twice_with_twin_parity() {
  const Frame f = encode(TRONFERNO_HEADER, noLeadIn());
  // Word 1 (second copy of 0x80, parity 1 -> 0x180) starts after word 0.
  const size_t word1 = 7 + 11 + 1;
  assertMark(400, 3200, f.marks[word1 - 1]);
  assertMark(400, 800, f.marks[word1 + 7]);  // data bit 7 = 1
  assertMark(400, 800, f.marks[word1 + 8]);  // parity bit 0 = 1
  assertMark(800, 400, f.marks[word1 + 9]);  // parity bit 1 = 0
}

static void test_frame_ends_with_lead_out() {
  const Frame f = encode(TRONFERNO_HEADER);
  assertMark(400, 42800, f.marks[f.count - 1]);
}

static void test_frame_duration() {
  // preamble 5.6 ms + 12 words x 15.6 ms + lead-out 43.2 ms (+ lead-in 50 ms)
  TEST_ASSERT_EQUAL_UINT32(5600 + 12 * 15600 + 43200, encode(TRONFERNO_HEADER, noLeadIn()).durationUs());
  TEST_ASSERT_EQUAL_UINT32(50000 + 5600 + 12 * 15600 + 43200, encode(TRONFERNO_HEADER).durationUs());
}

static void test_all_durations_fit_the_rmt_at_400khz() {
  // 2.5 us ticks, 15-bit duration field: whole ticks up to 81917.5 us.
  const Frame f = encode(TRONFERNO_HEADER);
  for (size_t i = 0; i < f.count; i++) {
    const uint32_t durations[2] = {f.marks[i].highUs, f.marks[i].lowUs};
    for (uint32_t us : durations) {
      TEST_ASSERT_TRUE(us > 0);
      TEST_ASSERT_EQUAL_UINT32(0, (us * 2) % 5);
      TEST_ASSERT_TRUE(us * 2 / 5 <= 32767);
    }
  }
}

static void test_preamble_length_is_configurable() {
  Timing t = noLeadIn();
  t.preambleCount = 6;
  TEST_ASSERT_EQUAL_UINT32(6 + 12 * 11 + 1, encode(TRONFERNO_HEADER, t).count);
}

static void test_on_air_time_excludes_trailing_gap() {
  const Frame f = encode(TRONFERNO_HEADER);
  TEST_ASSERT_EQUAL_UINT32(f.durationUs() - 42800, f.onAirUs());
  TEST_ASSERT_EQUAL_UINT32(50000 + 5600 + 12 * 15600 + 400, f.onAirUs());
}

// --- Duty-cycle budget -------------------------------------------------------

static void test_airtime_refuses_past_budget() {
  Airtime a(1000);
  TEST_ASSERT_TRUE(a.tryConsume(0, 600));
  TEST_ASSERT_TRUE(a.tryConsume(10, 400));
  TEST_ASSERT_FALSE(a.tryConsume(20, 1));
  TEST_ASSERT_EQUAL_UINT32(1000, a.usedMs(20));
}

static void test_airtime_refused_request_books_nothing() {
  Airtime a(1000);
  TEST_ASSERT_TRUE(a.tryConsume(0, 900));
  TEST_ASSERT_FALSE(a.tryConsume(0, 200));
  TEST_ASSERT_TRUE(a.tryConsume(0, 100));
}

static void test_airtime_expires_after_an_hour() {
  Airtime a(1000);
  a.tryConsume(0, 1000);
  TEST_ASSERT_FALSE(a.tryConsume(59 * 60000, 1));
  TEST_ASSERT_TRUE(a.tryConsume(60 * 60000, 1000));
}

static void test_airtime_expires_minute_by_minute() {
  Airtime a(1000);
  a.tryConsume(0, 300);           // minute 0
  a.tryConsume(30 * 60000, 700);  // minute 30
  TEST_ASSERT_EQUAL_UINT32(1000, a.usedMs(59 * 60000));
  TEST_ASSERT_EQUAL_UINT32(700, a.usedMs(60 * 60000));  // minute 0 dropped
  TEST_ASSERT_EQUAL_UINT32(0, a.usedMs(90 * 60000));
}

static void test_airtime_counter_wrap_clears_history() {
  Airtime a(1000);
  a.tryConsume(0xFFFFFFF0u, 1000);
  TEST_ASSERT_TRUE(a.tryConsume(5, 1000));
}

static void test_default_budget_allows_hundreds_of_commands_per_hour() {
  // 4 repeats of a frame with lead-in, counted generously.
  Airtime a;
  const uint32_t perCommandMs = 4 * (encode(TRONFERNO_HEADER).onAirUs() / 1000 + 1);
  uint32_t sent = 0;
  while (a.tryConsume(sent, perCommandMs)) sent++;
  TEST_ASSERT_TRUE(sent >= 300);
  TEST_ASSERT_TRUE(sent <= 500);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_checksum_is_byte_sum);
  RUN_TEST(test_builds_tronferno_reference_header);
  RUN_TEST(test_central_unit_group_and_member);
  RUN_TEST(test_central_unit_broadcast_is_zero_nibbles);
  RUN_TEST(test_motor_code_targets_receiver);
  RUN_TEST(test_rejects_out_of_range_group_and_member);
  RUN_TEST(test_rejects_counter_outside_1_to_15);
  RUN_TEST(test_rejects_unknown_device_type);
  RUN_TEST(test_refuses_setup_commands_to_broadcast);
  RUN_TEST(test_counter_wraps_and_skips_zero);
  RUN_TEST(test_parity_depends_on_bit_count_and_twin_position);
  RUN_TEST(test_frame_layout_counts);
  RUN_TEST(test_frame_starts_with_lead_in_then_preamble_then_stop);
  RUN_TEST(test_first_word_bits_go_out_lsb_first_with_parity);
  RUN_TEST(test_each_byte_is_sent_twice_with_twin_parity);
  RUN_TEST(test_frame_ends_with_lead_out);
  RUN_TEST(test_frame_duration);
  RUN_TEST(test_all_durations_fit_the_rmt_at_400khz);
  RUN_TEST(test_preamble_length_is_configurable);
  RUN_TEST(test_on_air_time_excludes_trailing_gap);
  RUN_TEST(test_airtime_refuses_past_budget);
  RUN_TEST(test_airtime_refused_request_books_nothing);
  RUN_TEST(test_airtime_expires_after_an_hour);
  RUN_TEST(test_airtime_expires_minute_by_minute);
  RUN_TEST(test_airtime_counter_wrap_clears_history);
  RUN_TEST(test_default_budget_allows_hundreds_of_commands_per_hour);
  return UNITY_END();
}
