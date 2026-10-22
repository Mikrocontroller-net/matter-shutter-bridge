#include <unity.h>

#include <string.h>

#include <config/command.h>
#include <config/house.h>

using namespace config;

void setUp() {}
void tearDown() {}

static Command run(std::initializer_list<const char *> words) {
  const char *argv[16];
  int argc = 0;
  for (const char *w : words) argv[argc++] = w;
  return parse(argc, argv);
}

// --- House model -------------------------------------------------------------

static void test_defaults_are_four_rooms_of_two() {
  const House h = defaults();
  TEST_ASSERT_EQUAL(Error::None, validate(h));
  TEST_ASSERT_EQUAL_UINT8(8, h.count);
  TEST_ASSERT_EQUAL_UINT8(1, h.shutters[0].group);
  TEST_ASSERT_EQUAL_UINT8(1, h.shutters[0].member);
  TEST_ASSERT_EQUAL_UINT8(4, h.shutters[7].group);
  TEST_ASSERT_EQUAL_UINT8(2, h.shutters[7].member);
  TEST_ASSERT_EQUAL_STRING("Raum 4 rechts", h.shutters[7].name);
}

static void test_set_name_accepts_utf8_up_to_32_bytes() {
  House h = defaults();
  TEST_ASSERT_EQUAL(Error::None, setName(h, 2, "Küche links"));
  TEST_ASSERT_EQUAL_STRING("Küche links", h.shutters[2].name);
  TEST_ASSERT_EQUAL(Error::None, setName(h, 2, "12345678901234567890123456789012"));
}

static void test_set_name_rejects_empty_long_and_control_chars() {
  House h = defaults();
  TEST_ASSERT_EQUAL(Error::BadName, setName(h, 2, ""));
  TEST_ASSERT_EQUAL(Error::BadName, setName(h, 2, "123456789012345678901234567890123"));
  TEST_ASSERT_EQUAL(Error::BadName, setName(h, 2, "tab\there"));
  TEST_ASSERT_EQUAL_STRING("Raum 2 links", h.shutters[2].name);
}

static void test_set_address_rejects_zero_and_duplicates() {
  House h = defaults();
  TEST_ASSERT_EQUAL(Error::BadGroup, setAddress(h, 0, 0, 1));
  TEST_ASSERT_EQUAL(Error::BadMember, setAddress(h, 0, 1, 0));
  TEST_ASSERT_EQUAL(Error::BadGroup, setAddress(h, 0, 8, 1));
  TEST_ASSERT_EQUAL(Error::DuplicateAddress, setAddress(h, 0, 1, 2));
  TEST_ASSERT_EQUAL(Error::None, setAddress(h, 0, 5, 3));
  TEST_ASSERT_EQUAL_UINT8(5, h.shutters[0].group);
}

static void test_set_times_bounds() {
  House h = defaults();
  TEST_ASSERT_EQUAL(Error::None, setTimes(h, 1, 21500, 19000));
  TEST_ASSERT_EQUAL_UINT32(21500, h.shutters[1].upMs);
  TEST_ASSERT_EQUAL_UINT32(19000, h.shutters[1].downMs);
  TEST_ASSERT_EQUAL(Error::BadTimes, setTimes(h, 1, 999, 19000));
  TEST_ASSERT_EQUAL(Error::BadTimes, setTimes(h, 1, 21500, 300001));
  TEST_ASSERT_EQUAL_UINT32(21500, h.shutters[1].upMs);
}

static void test_set_invert_flags_independently() {
  House h = defaults();
  TEST_ASSERT_EQUAL(Error::None, setInvert(h, 3, false, true));
  TEST_ASSERT_TRUE(h.shutters[3].invertEnds);
  TEST_ASSERT_FALSE(h.shutters[3].invertMid);
  TEST_ASSERT_EQUAL(Error::None, setInvert(h, 3, true, true));
  TEST_ASSERT_TRUE(h.shutters[3].invertMid);
}

static void test_out_of_range_index() {
  House h = defaults();
  TEST_ASSERT_EQUAL(Error::BadIndex, setName(h, 8, "x"));
  TEST_ASSERT_EQUAL(Error::BadIndex, setTimes(h, 15, 2000, 2000));
}

static void test_central_id_needs_central_unit_type() {
  House h = defaults();
  TEST_ASSERT_EQUAL(Error::None, setCentralId(h, 0x80495d));
  TEST_ASSERT_EQUAL_HEX32(0x80495d, h.centralId);
  TEST_ASSERT_EQUAL(Error::BadCentralId, setCentralId(h, 0x10495d));
  TEST_ASSERT_EQUAL(Error::BadCentralId, setCentralId(h, 0x180495d));
  TEST_ASSERT_EQUAL_HEX32(0x80495d, h.centralId);
}

static void test_repeats_bounds() {
  House h = defaults();
  TEST_ASSERT_EQUAL(Error::BadRepeats, setRepeats(h, 0));
  TEST_ASSERT_EQUAL(Error::BadRepeats, setRepeats(h, 9));
  TEST_ASSERT_EQUAL(Error::None, setRepeats(h, 2));
  TEST_ASSERT_EQUAL_UINT8(2, h.repeats);
}

// --- Blob --------------------------------------------------------------------

static void test_blob_round_trip() {
  House h = defaults();
  setName(h, 0, "Küche links");
  setTimes(h, 0, 21500, 19000);
  setInvert(h, 0, true, true);
  setCentralId(h, 0x80495d);
  uint8_t blob[MAX_BLOB];
  const size_t n = serialize(h, blob, sizeof(blob));
  TEST_ASSERT_EQUAL_UINT32(8 + 8 * 44, n);

  House back;
  TEST_ASSERT_EQUAL(Error::None, deserialize(blob, n, back));
  TEST_ASSERT_EQUAL_UINT8(8, back.count);
  TEST_ASSERT_EQUAL_HEX32(0x80495d, back.centralId);
  TEST_ASSERT_EQUAL_STRING("Küche links", back.shutters[0].name);
  TEST_ASSERT_EQUAL_UINT32(21500, back.shutters[0].upMs);
  TEST_ASSERT_TRUE(back.shutters[0].invertMid);
  TEST_ASSERT_FALSE(back.shutters[0].invertEnds);
  TEST_ASSERT_EQUAL_UINT8(2, back.shutters[7].member);
}

static void test_blob_rejects_truncated_extra_and_wrong_version() {
  const House h = defaults();
  uint8_t blob[MAX_BLOB + 1];
  const size_t n = serialize(h, blob, MAX_BLOB);
  House back;
  TEST_ASSERT_EQUAL(Error::BadBlob, deserialize(blob, n - 1, back));
  TEST_ASSERT_EQUAL(Error::BadBlob, deserialize(blob, n + 1, back));
  blob[0] = 99;
  TEST_ASSERT_EQUAL(Error::BadBlob, deserialize(blob, n, back));
}

static void test_blob_rejects_invalid_content() {
  House h = defaults();
  uint8_t blob[MAX_BLOB];
  const size_t n = serialize(h, blob, sizeof(blob));
  // Shutter 0's group byte sits right after the header and its 33-byte name.
  blob[8 + 33] = 0;
  House back = defaults();
  setName(back, 0, "unchanged");
  TEST_ASSERT_EQUAL(Error::BadGroup, deserialize(blob, n, back));
  TEST_ASSERT_EQUAL_STRING("unchanged", back.shutters[0].name);
}

static void test_serialize_reports_short_buffer() {
  uint8_t small[16];
  TEST_ASSERT_EQUAL_UINT32(0, serialize(defaults(), small, sizeof(small)));
}

// --- Console commands --------------------------------------------------------

static void test_parse_list_and_show() {
  TEST_ASSERT_EQUAL(Op::List, run({"shutter"}).op);
  TEST_ASSERT_EQUAL(Op::ShowHouse, run({"house"}).op);
}

static void test_parse_name_joins_words() {
  const Command c = run({"shutter", "3", "name", "Küche", "links"});
  TEST_ASSERT_EQUAL(Op::Name, c.op);
  TEST_ASSERT_EQUAL_UINT32(2, c.index);
  TEST_ASSERT_EQUAL_STRING("Küche links", c.text);
}

static void test_parse_name_too_long() {
  const Command c = run({"shutter", "1", "name", "1234567890123456", "1234567890123456"});
  TEST_ASSERT_EQUAL(Op::Invalid, c.op);
  TEST_ASSERT_NOT_NULL(c.usage);
}

static void test_parse_address_times_invert() {
  Command c = run({"shutter", "8", "address", "4", "2"});
  TEST_ASSERT_EQUAL(Op::Address, c.op);
  TEST_ASSERT_EQUAL_UINT32(7, c.index);
  TEST_ASSERT_EQUAL_UINT32(4, c.a);
  TEST_ASSERT_EQUAL_UINT32(2, c.b);

  c = run({"shutter", "1", "times", "21500", "19000"});
  TEST_ASSERT_EQUAL(Op::Times, c.op);
  TEST_ASSERT_EQUAL_UINT32(21500, c.a);
  TEST_ASSERT_EQUAL_UINT32(19000, c.b);

  c = run({"shutter", "2", "invert", "mid", "on"});
  TEST_ASSERT_EQUAL(Op::Invert, c.op);
  TEST_ASSERT_TRUE(c.mid);
  TEST_ASSERT_TRUE(c.on);
}

static void test_parse_rejects_bad_shutter_input() {
  TEST_ASSERT_EQUAL(Op::Invalid, run({"shutter", "0", "times", "1", "2"}).op);
  TEST_ASSERT_EQUAL(Op::Invalid, run({"shutter", "x", "times", "1", "2"}).op);
  TEST_ASSERT_EQUAL(Op::Invalid, run({"shutter", "1", "times", "12a", "2"}).op);
  TEST_ASSERT_EQUAL(Op::Invalid, run({"shutter", "1", "invert", "both", "on"}).op);
  TEST_ASSERT_EQUAL(Op::Invalid, run({"shutter", "1", "colour", "red"}).op);
}

static void test_parse_house_id_accepts_plain_and_0x_hex() {
  TEST_ASSERT_EQUAL_HEX32(0x80495d, run({"house", "id", "80495d"}).a);
  TEST_ASSERT_EQUAL_HEX32(0x80495d, run({"house", "id", "0x80495D"}).a);
  TEST_ASSERT_EQUAL(Op::Invalid, run({"house", "id", "zz"}).op);
  TEST_ASSERT_EQUAL(Op::Repeats, run({"house", "repeats", "3"}).op);
}

static void test_parse_calibration() {
  Command c = run({"cal", "3", "down"});
  TEST_ASSERT_EQUAL(Op::CalStart, c.op);
  TEST_ASSERT_EQUAL_UINT32(2, c.index);
  TEST_ASSERT_FALSE(c.on);
  TEST_ASSERT_TRUE(run({"cal", "3", "up"}).on);
  TEST_ASSERT_EQUAL(Op::CalDone, run({"cal", "done"}).op);
  TEST_ASSERT_EQUAL(Op::CalCancel, run({"cal", "cancel"}).op);
  TEST_ASSERT_EQUAL(Op::Invalid, run({"cal", "3", "left"}).op);
}

static void test_apply_changes_house_and_reports_errors() {
  House h = defaults();
  TEST_ASSERT_EQUAL(Error::None, apply(h, run({"shutter", "3", "name", "Bad", "oben"})));
  TEST_ASSERT_EQUAL_STRING("Bad oben", h.shutters[2].name);
  TEST_ASSERT_EQUAL(Error::DuplicateAddress, apply(h, run({"shutter", "3", "address", "1", "1"})));
  TEST_ASSERT_EQUAL(Error::BadGroup, apply(h, run({"shutter", "3", "address", "300", "1"})));
  TEST_ASSERT_EQUAL(Error::BadIndex, apply(h, run({"shutter", "9", "times", "2000", "2000"})));
  TEST_ASSERT_EQUAL(Error::BadRepeats, apply(h, run({"house", "repeats", "300"})));
  TEST_ASSERT_EQUAL(Error::None, apply(h, run({"house", "id", "80495d"})));
  TEST_ASSERT_EQUAL_HEX32(0x80495d, h.centralId);
}

static void test_parse_send() {
  Command c = run({"send", "1", "2", "up"});
  TEST_ASSERT_EQUAL(Op::Send, c.op);
  TEST_ASSERT_EQUAL_UINT32(1, c.a);
  TEST_ASSERT_EQUAL_UINT32(2, c.b);
  TEST_ASSERT_EQUAL(Move::Up, c.move);
  TEST_ASSERT_EQUAL(Move::Stop, run({"send", "0", "0", "stop"}).move);
  TEST_ASSERT_EQUAL(Move::Down, run({"send", "4", "0", "down"}).move);
  TEST_ASSERT_EQUAL(Op::Invalid, run({"send", "8", "1", "up"}).op);
  TEST_ASSERT_EQUAL(Op::Invalid, run({"send", "1", "1", "set"}).op);
  TEST_ASSERT_EQUAL(Op::Invalid, run({"send", "1", "up"}).op);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_defaults_are_four_rooms_of_two);
  RUN_TEST(test_set_name_accepts_utf8_up_to_32_bytes);
  RUN_TEST(test_set_name_rejects_empty_long_and_control_chars);
  RUN_TEST(test_set_address_rejects_zero_and_duplicates);
  RUN_TEST(test_set_times_bounds);
  RUN_TEST(test_set_invert_flags_independently);
  RUN_TEST(test_out_of_range_index);
  RUN_TEST(test_central_id_needs_central_unit_type);
  RUN_TEST(test_repeats_bounds);
  RUN_TEST(test_blob_round_trip);
  RUN_TEST(test_blob_rejects_truncated_extra_and_wrong_version);
  RUN_TEST(test_blob_rejects_invalid_content);
  RUN_TEST(test_serialize_reports_short_buffer);
  RUN_TEST(test_parse_list_and_show);
  RUN_TEST(test_parse_name_joins_words);
  RUN_TEST(test_parse_name_too_long);
  RUN_TEST(test_parse_address_times_invert);
  RUN_TEST(test_parse_rejects_bad_shutter_input);
  RUN_TEST(test_parse_house_id_accepts_plain_and_0x_hex);
  RUN_TEST(test_parse_calibration);
  RUN_TEST(test_apply_changes_house_and_reports_errors);
  RUN_TEST(test_parse_send);
  return UNITY_END();
}
