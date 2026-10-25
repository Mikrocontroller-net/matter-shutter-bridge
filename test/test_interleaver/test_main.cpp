#include <unity.h>

#include <string>

#include <fernotron/interleaver.h>

using namespace fernotron;

void setUp() {}
void tearDown() {}

// `tag` lands in the message so the tests can tell copies apart.
static Transmission tx(char tag, uint8_t group, uint8_t member, Command cmd = Command::Up, uint8_t copies = 4) {
  Transmission t{};
  t.message[3] = (uint8_t)tag;
  t.frame = {group, member, cmd, 1};
  t.copiesLeft = copies;
  return t;
}

// Tags of all remaining copies, in send order.
static std::string drain(Interleaver &il) {
  std::string order;
  Transmission t;
  while (il.next(t)) order += (char)t.message[3];
  return order;
}

static void test_empty_sends_nothing() {
  Interleaver il;
  Transmission t;
  TEST_ASSERT_TRUE(il.empty());
  TEST_ASSERT_FALSE(il.next(t));
}

static void test_single_command_sends_all_copies() {
  Interleaver il;
  il.add(tx('A', 1, 1));
  TEST_ASSERT_EQUAL_STRING("AAAA", drain(il).c_str());
  TEST_ASSERT_TRUE(il.empty());
}

static void test_commands_take_turns() {
  Interleaver il;
  il.add(tx('A', 1, 0));
  il.add(tx('B', 2, 0));
  il.add(tx('C', 3, 0));
  il.add(tx('D', 4, 0));
  TEST_ASSERT_EQUAL_STRING("ABCDABCDABCDABCD", drain(il).c_str());
}

static void test_different_copy_counts() {
  Interleaver il;
  il.add(tx('A', 1, 1, Command::Up, 2));
  il.add(tx('B', 2, 1, Command::Up, 4));
  TEST_ASSERT_EQUAL_STRING("ABABBB", drain(il).c_str());
}

static void test_every_copy_carries_the_same_message() {
  Interleaver il;
  Transmission a = tx('A', 1, 1);
  a.message[4] = 0x55;
  il.add(a);
  Transmission t;
  uint8_t left = 4;
  while (il.next(t)) {
    TEST_ASSERT_EQUAL_HEX8(0x55, t.message[4]);
    TEST_ASSERT_EQUAL_UINT8(--left, t.copiesLeft);
  }
}

static void test_new_command_joins_the_round() {
  Interleaver il;
  il.add(tx('A', 1, 0, Command::Up, 3));
  il.add(tx('B', 2, 0, Command::Up, 3));
  Transmission t;
  il.next(t);  // A
  il.add(tx('C', 3, 0, Command::Up, 3));
  TEST_ASSERT_EQUAL_STRING("BCABCABC", drain(il).c_str());
}

static void test_stop_goes_next() {
  Interleaver il;
  il.add(tx('A', 1, 0));
  il.add(tx('B', 2, 0));
  Transmission t;
  il.next(t);  // A
  il.add(tx('S', 3, 1, Command::Stop));
  il.next(t);
  TEST_ASSERT_EQUAL_CHAR('S', t.message[3]);
}

static void test_newer_command_drops_older_copies_for_the_same_shutter() {
  Interleaver il;
  il.add(tx('U', 1, 1, Command::Up));
  Transmission t;
  il.next(t);
  il.add(tx('S', 1, 1, Command::Stop));
  TEST_ASSERT_EQUAL_STRING("SSSS", drain(il).c_str());
}

static void test_overlap_through_group_and_broadcast_addresses() {
  Interleaver il;
  il.add(tx('U', 0, 0));  // whole house
  il.add(tx('D', 2, 1, Command::Down));
  TEST_ASSERT_EQUAL_STRING("DDDD", drain(il).c_str());

  il.add(tx('U', 1, 0));  // room 1
  il.add(tx('S', 1, 2, Command::Stop));
  TEST_ASSERT_EQUAL_STRING("SSSS", drain(il).c_str());
}

static void test_other_shutters_keep_their_copies() {
  Interleaver il;
  il.add(tx('A', 1, 1));
  il.add(tx('B', 1, 2));  // same room, other shutter
  il.add(tx('C', 2, 1));  // other room, same member number
  TEST_ASSERT_EQUAL_STRING("ABCABCABCABC", drain(il).c_str());
}

static void test_pairs_go_out_back_to_back() {
  Interleaver il(2);
  il.add(tx('A', 1, 0));
  il.add(tx('B', 2, 0));
  TEST_ASSERT_EQUAL_STRING("AABBAABB", drain(il).c_str());
}

static void test_pairs_with_an_odd_copy_count() {
  Interleaver il(2);
  il.add(tx('A', 1, 0, Command::Up, 3));
  il.add(tx('B', 2, 0, Command::Up, 3));
  TEST_ASSERT_EQUAL_STRING("AABBAB", drain(il).c_str());
}

static void test_stop_gets_a_full_pair_even_mid_turn() {
  Interleaver il(2);
  il.add(tx('A', 1, 0));
  il.add(tx('B', 2, 0));
  Transmission t;
  il.next(t);  // first copy of A's pair
  il.add(tx('S', 3, 1, Command::Stop));
  TEST_ASSERT_EQUAL_STRING("SSAABBSSABB", drain(il).c_str());
}

static void test_full_rotation_refuses_more() {
  Interleaver il;
  size_t added = 0;
  for (uint8_t g = 1; g <= 7; g++) {
    for (uint8_t m = 1; m <= 7; m++) {
      if (il.add(tx('x', g, m))) added++;
    }
  }
  TEST_ASSERT_EQUAL(Interleaver::CAPACITY, added);
}

static void test_zero_copies_are_refused() {
  Interleaver il;
  TEST_ASSERT_FALSE(il.add(tx('A', 1, 1, Command::Up, 0)));
  TEST_ASSERT_TRUE(il.empty());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_empty_sends_nothing);
  RUN_TEST(test_single_command_sends_all_copies);
  RUN_TEST(test_commands_take_turns);
  RUN_TEST(test_different_copy_counts);
  RUN_TEST(test_every_copy_carries_the_same_message);
  RUN_TEST(test_new_command_joins_the_round);
  RUN_TEST(test_stop_goes_next);
  RUN_TEST(test_newer_command_drops_older_copies_for_the_same_shutter);
  RUN_TEST(test_overlap_through_group_and_broadcast_addresses);
  RUN_TEST(test_other_shutters_keep_their_copies);
  RUN_TEST(test_pairs_go_out_back_to_back);
  RUN_TEST(test_pairs_with_an_odd_copy_count);
  RUN_TEST(test_stop_gets_a_full_pair_even_mid_turn);
  RUN_TEST(test_full_rotation_refuses_more);
  RUN_TEST(test_zero_copies_are_refused);
  return UNITY_END();
}
