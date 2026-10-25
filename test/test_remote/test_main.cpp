#include <unity.h>

#include <remote/log_ring.h>
#include <remote/token.h>

#include <string.h>
#include <string>

using namespace remote;

void setUp() {}
void tearDown() {}

static void append(LogRing<8> &ring, const char *s) { ring.append(s, strlen(s)); }

static std::string readAll(const LogRing<8> &ring, uint64_t &from, size_t cap = 64) {
  char out[64];
  const size_t n = ring.read(from, out, cap);
  from += n;
  return std::string(out, n);
}

static void test_ring_reads_what_was_appended() {
  LogRing<8> ring;
  append(ring, "abc");
  uint64_t from = 0;
  TEST_ASSERT_EQUAL_STRING("abc", readAll(ring, from).c_str());
  TEST_ASSERT_EQUAL_UINT64(3, from);
  TEST_ASSERT_EQUAL_STRING("", readAll(ring, from).c_str());
}

static void test_ring_continues_from_the_last_read() {
  LogRing<8> ring;
  append(ring, "abc");
  uint64_t from = 0;
  readAll(ring, from);
  append(ring, "de");
  TEST_ASSERT_EQUAL_STRING("de", readAll(ring, from).c_str());
  TEST_ASSERT_EQUAL_UINT64(5, ring.end());
}

static void test_ring_keeps_only_the_newest_bytes() {
  LogRing<8> ring;
  append(ring, "0123456789");
  TEST_ASSERT_EQUAL_UINT64(2, ring.begin());
  uint64_t from = 0;
  TEST_ASSERT_EQUAL_STRING("23456789", readAll(ring, from).c_str());
  TEST_ASSERT_EQUAL_UINT64(10, from);
}

static void test_ring_moves_a_lagging_reader_to_the_oldest_byte() {
  LogRing<8> ring;
  append(ring, "abc");
  uint64_t from = 1;
  append(ring, "defghijk");  // "abc" is gone
  char out[8];
  const size_t n = ring.read(from, out, sizeof(out));
  TEST_ASSERT_EQUAL_UINT64(3, from);
  TEST_ASSERT_EQUAL(8, n);
  TEST_ASSERT_EQUAL_MEMORY("defghijk", out, 8);
}

static void test_ring_read_respects_capacity() {
  LogRing<8> ring;
  append(ring, "abcdef");
  uint64_t from = 0;
  TEST_ASSERT_EQUAL_STRING("abcd", readAll(ring, from, 4).c_str());
  TEST_ASSERT_EQUAL_STRING("ef", readAll(ring, from, 4).c_str());
}

static void test_ring_reader_ahead_gets_nothing() {
  LogRing<8> ring;
  append(ring, "abc");
  uint64_t from = 10;
  char out[8];
  TEST_ASSERT_EQUAL(0, ring.read(from, out, sizeof(out)));
  TEST_ASSERT_EQUAL_UINT64(10, from);
}

static void test_make_token_is_hex_of_the_words() {
  const uint32_t words[] = {0x76543210, 0xfedcba98, 0, 0xffffffff};
  size_t next = 0;
  Token t = makeToken([&] { return words[next++]; });
  TEST_ASSERT_EQUAL_STRING("0123456789abcdef00000000ffffffff", t.data());
  TEST_ASSERT_TRUE(isValidToken(t.data()));
}

static void test_valid_token_rules() {
  TEST_ASSERT_TRUE(isValidToken("0123456789abcdef0123456789abcdef"));
  TEST_ASSERT_FALSE(isValidToken(nullptr));
  TEST_ASSERT_FALSE(isValidToken(""));
  TEST_ASSERT_FALSE(isValidToken("0123456789abcdef0123456789abcde"));
  TEST_ASSERT_FALSE(isValidToken("0123456789abcdef0123456789abcdef0"));
  TEST_ASSERT_FALSE(isValidToken("0123456789ABCDEF0123456789abcdef"));
  TEST_ASSERT_FALSE(isValidToken("0123456789abcdeg0123456789abcdef"));
}

static void test_authorized_needs_the_exact_bearer_header() {
  const char *token = "0123456789abcdef0123456789abcdef";
  TEST_ASSERT_TRUE(authorized("Bearer 0123456789abcdef0123456789abcdef", token));
  TEST_ASSERT_FALSE(authorized(nullptr, token));
  TEST_ASSERT_FALSE(authorized("", token));
  TEST_ASSERT_FALSE(authorized("0123456789abcdef0123456789abcdef", token));
  TEST_ASSERT_FALSE(authorized("Basic 0123456789abcdef0123456789abcdef", token));
  TEST_ASSERT_FALSE(authorized("Bearer 0123456789abcdef0123456789abcdee", token));
  TEST_ASSERT_FALSE(authorized("Bearer 0123456789abcdef0123456789abcdef ", token));
  TEST_ASSERT_FALSE(authorized("Bearer 0123456789abcdef", token));
}

static void test_authorized_rejects_an_invalid_stored_token() {
  TEST_ASSERT_FALSE(authorized("Bearer ", ""));
  TEST_ASSERT_FALSE(authorized("Bearer x", "x"));
  TEST_ASSERT_FALSE(authorized("Bearer 0123456789abcdef0123456789abcdef", nullptr));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_ring_reads_what_was_appended);
  RUN_TEST(test_ring_continues_from_the_last_read);
  RUN_TEST(test_ring_keeps_only_the_newest_bytes);
  RUN_TEST(test_ring_moves_a_lagging_reader_to_the_oldest_byte);
  RUN_TEST(test_ring_read_respects_capacity);
  RUN_TEST(test_ring_reader_ahead_gets_nothing);
  RUN_TEST(test_make_token_is_hex_of_the_words);
  RUN_TEST(test_valid_token_rules);
  RUN_TEST(test_authorized_needs_the_exact_bearer_header);
  RUN_TEST(test_authorized_rejects_an_invalid_stored_token);
  return UNITY_END();
}
