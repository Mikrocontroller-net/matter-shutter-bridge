#include <unity.h>

#include <pairing/pairing.h>

using namespace pairing;

void setUp() {}
void tearDown() {}

// Hands out the given words in order, then repeats the last one.
struct Words {
  const uint32_t *words;
  size_t count;
  size_t next = 0;
  uint32_t operator()() { return words[next < count ? next++ : count - 1]; }
};

static void test_passcode_rules() {
  TEST_ASSERT_FALSE(isValidPasscode(0));
  TEST_ASSERT_TRUE(isValidPasscode(1));
  TEST_ASSERT_TRUE(isValidPasscode(MAX_PASSCODE));
  TEST_ASSERT_FALSE(isValidPasscode(MAX_PASSCODE + 1));
  TEST_ASSERT_FALSE(isValidPasscode(11111111));
  TEST_ASSERT_FALSE(isValidPasscode(88888888));
  TEST_ASSERT_FALSE(isValidPasscode(12345678));
  TEST_ASSERT_FALSE(isValidPasscode(87654321));
  TEST_ASSERT_TRUE(isValidPasscode(20202021));
  TEST_ASSERT_TRUE(isValidPasscode(99999999 - 2));
}

static void test_generate_maps_words_into_range() {
  const uint32_t words[] = {0, 0xFFFFFFFF, 0x01020304};
  Code c = generate(Words{words, 3});
  TEST_ASSERT_EQUAL_UINT32(1, c.passcode);
  TEST_ASSERT_EQUAL_UINT16(0x0FFF, c.discriminator);
  TEST_ASSERT_EQUAL_HEX8(0x04, c.salt[0]);
  TEST_ASSERT_EQUAL_HEX8(0x01, c.salt[3]);
  TEST_ASSERT_TRUE(isValidPasscode(c.passcode));
}

static void test_generate_draws_again_for_trivial_passcodes() {
  // 11111110 % MAX_PASSCODE + 1 = 11111111, which is not allowed.
  const uint32_t words[] = {11111110, 12345677, 41, 7};
  Code c = generate(Words{words, 4});
  TEST_ASSERT_EQUAL_UINT32(42, c.passcode);
  TEST_ASSERT_EQUAL_UINT16(7, c.discriminator);
}

static void test_generate_fills_the_whole_salt() {
  uint32_t words[2 + SALT_BYTES / 4];
  words[0] = 5;
  words[1] = 6;
  for (size_t i = 2; i < sizeof(words) / sizeof(words[0]); i++) words[i] = 0xA5A5A5A5;
  Code c = generate(Words{words, sizeof(words) / sizeof(words[0])});
  for (uint8_t s : c.salt) TEST_ASSERT_EQUAL_HEX8(0xA5, s);
}

static Code sample() {
  Code c;
  c.passcode = 34567890;
  c.discriminator = 0x0ABC;
  for (size_t i = 0; i < SALT_BYTES; i++) c.salt[i] = (uint8_t)(i * 7);
  return c;
}

static void test_round_trip() {
  uint8_t blob[BLOB_BYTES];
  TEST_ASSERT_EQUAL(BLOB_BYTES, serialize(sample(), blob, sizeof(blob)));
  Code back;
  TEST_ASSERT_TRUE(deserialize(blob, sizeof(blob), back));
  TEST_ASSERT_EQUAL_UINT32(34567890, back.passcode);
  TEST_ASSERT_EQUAL_UINT16(0x0ABC, back.discriminator);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(sample().salt.data(), back.salt.data(), SALT_BYTES);
}

static void test_serialize_needs_room() {
  uint8_t blob[BLOB_BYTES - 1];
  TEST_ASSERT_EQUAL(0, serialize(sample(), blob, sizeof(blob)));
}

static void test_deserialize_rejects_malformed_blobs() {
  uint8_t blob[BLOB_BYTES];
  serialize(sample(), blob, sizeof(blob));
  Code out;
  TEST_ASSERT_FALSE(deserialize(blob, sizeof(blob) - 1, out));

  blob[0] = 2;  // unknown version
  TEST_ASSERT_FALSE(deserialize(blob, sizeof(blob), out));

  Code trivial = sample();
  trivial.passcode = 12345678;
  serialize(trivial, blob, sizeof(blob));
  TEST_ASSERT_FALSE(deserialize(blob, sizeof(blob), out));

  Code wide = sample();
  wide.discriminator = 0x1000;
  serialize(wide, blob, sizeof(blob));
  TEST_ASSERT_FALSE(deserialize(blob, sizeof(blob), out));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_passcode_rules);
  RUN_TEST(test_generate_maps_words_into_range);
  RUN_TEST(test_generate_draws_again_for_trivial_passcodes);
  RUN_TEST(test_generate_fills_the_whole_salt);
  RUN_TEST(test_round_trip);
  RUN_TEST(test_serialize_needs_room);
  RUN_TEST(test_deserialize_rejects_malformed_blobs);
  return UNITY_END();
}
