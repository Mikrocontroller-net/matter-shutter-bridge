#include <unity.h>

#include <fernotron/batcher.h>

using namespace fernotron;

static constexpr uint32_t QUIET = 300;
static constexpr uint32_t MAX_WAIT = 2000;

// This house: groups 1-4 with members 1 and 2.
static Batcher make() {
  Batcher b(QUIET, MAX_WAIT);
  for (uint8_t g = 1; g <= 4; g++) {
    b.addMember(g, 1);
    b.addMember(g, 2);
  }
  return b;
}

static void assertFrame(uint8_t group, uint8_t member, Command cmd, const Batcher::Frame &f) {
  TEST_ASSERT_EQUAL_UINT8(group, f.group);
  TEST_ASSERT_EQUAL_UINT8(member, f.member);
  TEST_ASSERT_EQUAL(cmd, f.cmd);
}

void setUp() {}
void tearDown() {}

static void test_single_command_waits_for_the_quiet_period() {
  Batcher b = make();
  Batcher::Frame f;
  b.add(1, 1, Command::Up, 1000);
  TEST_ASSERT_FALSE(b.next(1000 + QUIET - 1, f));
  TEST_ASSERT_TRUE(b.next(1000 + QUIET, f));
  assertFrame(1, 1, Command::Up, f);
  TEST_ASSERT_FALSE(b.next(5000, f));
}

static void test_whole_room_becomes_one_group_frame() {
  Batcher b = make();
  Batcher::Frame f;
  b.add(1, 1, Command::Down, 0);
  b.add(1, 2, Command::Down, 40);
  TEST_ASSERT_TRUE(b.next(40 + QUIET, f));
  assertFrame(1, 0, Command::Down, f);
  TEST_ASSERT_EQUAL_UINT8(2, f.merged);
  TEST_ASSERT_FALSE(b.next(40 + QUIET, f));
}

static void test_whole_house_becomes_one_broadcast() {
  // An Alexa routine: one command per shutter, about 200 ms apart.
  Batcher b = make();
  Batcher::Frame f;
  uint32_t t = 0;
  for (uint8_t g = 1; g <= 4; g++) {
    for (uint8_t m = 1; m <= 2; m++) {
      b.add(g, m, Command::Up, t);
      TEST_ASSERT_FALSE(b.next(t + 199, f));
      t += 200;
    }
  }
  const uint32_t last = t - 200;
  TEST_ASSERT_FALSE(b.next(last + QUIET - 1, f));
  TEST_ASSERT_TRUE(b.next(last + QUIET, f));
  assertFrame(0, 0, Command::Up, f);
  TEST_ASSERT_EQUAL_UINT8(8, f.merged);
  TEST_ASSERT_FALSE(b.next(5000, f));
}

static void test_each_command_extends_the_quiet_period() {
  Batcher b = make();
  Batcher::Frame f;
  b.add(1, 1, Command::Up, 0);
  b.add(2, 1, Command::Up, 250);
  TEST_ASSERT_FALSE(b.next(QUIET, f));
  TEST_ASSERT_FALSE(b.next(250 + QUIET - 1, f));
  TEST_ASSERT_TRUE(b.next(250 + QUIET, f));
}

static void test_gap_longer_than_quiet_period_splits_batches() {
  Batcher b = make();
  Batcher::Frame f;
  b.add(1, 1, Command::Up, 0);
  TEST_ASSERT_TRUE(b.next(QUIET, f));
  assertFrame(1, 1, Command::Up, f);
  b.add(1, 2, Command::Up, QUIET + 100);
  TEST_ASSERT_TRUE(b.next(2 * QUIET + 100, f));
  assertFrame(1, 2, Command::Up, f);
}

static void test_steady_stream_leaves_at_maximum_wait() {
  // Commands keep coming faster than the quiet period; the batch still leaves
  // MAX_WAIT after its first command.
  Batcher b = make();
  Batcher::Frame f;
  for (uint32_t t = 0; t <= MAX_WAIT + 400; t += 200) {
    if (t < MAX_WAIT) TEST_ASSERT_FALSE(b.next(t, f));
    b.add((uint8_t)(1 + (t / 200) % 4), 1, Command::Down, t);
    if (t == MAX_WAIT) TEST_ASSERT_TRUE(b.next(t, f));
  }
}

static void test_command_arriving_mid_flush_joins_without_waiting() {
  Batcher b = make();
  Batcher::Frame f;
  b.add(1, 1, Command::Up, 0);
  b.add(2, 1, Command::Down, 0);
  TEST_ASSERT_TRUE(b.next(QUIET, f));  // first of the two goes out
  // While it was on air (~1 s), another command came in.
  b.add(3, 1, Command::Up, 1100);
  int frames = 0;
  while (b.next(1100, f)) frames++;
  TEST_ASSERT_EQUAL(2, frames);
}

static void test_incomplete_room_stays_individual() {
  Batcher b = make();
  Batcher::Frame f;
  // Everything but room 1's right shutter.
  b.add(1, 1, Command::Up, 0);
  for (uint8_t g = 2; g <= 4; g++) {
    b.add(g, 1, Command::Up, 0);
    b.add(g, 2, Command::Up, 0);
  }
  int groups = 0, singles = 0;
  while (b.next(QUIET, f)) {
    TEST_ASSERT_EQUAL(Command::Up, f.cmd);
    if (f.member == 0) {
      TEST_ASSERT_TRUE(f.group >= 2 && f.group <= 4);
      groups++;
    } else {
      assertFrame(1, 1, Command::Up, f);
      singles++;
    }
  }
  TEST_ASSERT_EQUAL(3, groups);
  TEST_ASSERT_EQUAL(1, singles);
}

static void test_different_commands_do_not_merge() {
  Batcher b = make();
  Batcher::Frame f;
  b.add(1, 1, Command::Up, 0);
  b.add(1, 2, Command::Down, 0);
  TEST_ASSERT_TRUE(b.next(QUIET, f));
  TEST_ASSERT_EQUAL_UINT8(1, f.merged);
  TEST_ASSERT_TRUE(b.next(QUIET, f));
  TEST_ASSERT_EQUAL_UINT8(1, f.merged);
  TEST_ASSERT_FALSE(b.next(QUIET, f));
}

static void test_latest_command_per_shutter_wins() {
  Batcher b = make();
  Batcher::Frame f;
  b.add(2, 1, Command::Up, 0);
  b.add(2, 1, Command::Down, 50);
  TEST_ASSERT_TRUE(b.next(50 + QUIET, f));
  assertFrame(2, 1, Command::Down, f);
  TEST_ASSERT_FALSE(b.next(1000, f));
}

static void test_replaced_command_keeps_its_place_in_line() {
  Batcher b = make();
  Batcher::Frame f;
  b.add(2, 1, Command::Up, 0);
  b.add(3, 1, Command::Up, 100);
  b.add(2, 1, Command::Down, 200);
  TEST_ASSERT_TRUE(b.next(200 + QUIET, f));
  assertFrame(2, 1, Command::Down, f);
}

static void test_stop_goes_first() {
  Batcher b = make();
  Batcher::Frame f;
  b.add(1, 1, Command::Up, 0);
  b.add(3, 1, Command::Up, 0);
  b.add(2, 2, Command::Stop, 10);
  TEST_ASSERT_TRUE(b.next(10 + QUIET, f));
  assertFrame(2, 2, Command::Stop, f);
}

static void test_stop_arriving_mid_flush_jumps_ahead() {
  Batcher b = make();
  Batcher::Frame f;
  b.add(1, 1, Command::Up, 0);
  b.add(2, 1, Command::Down, 0);
  TEST_ASSERT_TRUE(b.next(QUIET, f));  // first of the two goes out
  // While it was on air (~1 s), a STOP came in.
  b.add(4, 2, Command::Stop, 1100);
  TEST_ASSERT_TRUE(b.next(1300, f));
  assertFrame(4, 2, Command::Stop, f);
  TEST_ASSERT_TRUE(b.next(1300, f));
  TEST_ASSERT_NOT_EQUAL(Command::Stop, f.cmd);
}

static void test_whole_house_stop_is_one_frame() {
  Batcher b = make();
  Batcher::Frame f;
  for (uint8_t g = 1; g <= 4; g++) {
    b.add(g, 1, Command::Stop, 0);
    b.add(g, 2, Command::Stop, 0);
  }
  TEST_ASSERT_TRUE(b.next(QUIET, f));
  assertFrame(0, 0, Command::Stop, f);
}

static void test_single_member_group_is_not_widened() {
  Batcher b(QUIET, MAX_WAIT);
  b.addMember(5, 3);
  b.addMember(6, 1);
  b.addMember(6, 2);
  Batcher::Frame f;
  b.add(5, 3, Command::Up, 0);
  TEST_ASSERT_TRUE(b.next(QUIET, f));
  assertFrame(5, 3, Command::Up, f);
}

static void test_without_membership_nothing_merges() {
  Batcher b(QUIET, MAX_WAIT);
  Batcher::Frame f;
  b.add(1, 1, Command::Up, 0);
  b.add(1, 2, Command::Up, 0);
  int frames = 0;
  while (b.next(QUIET, f)) {
    TEST_ASSERT_NOT_EQUAL(0, f.member);
    frames++;
  }
  TEST_ASSERT_EQUAL(2, frames);
}

static void test_explicit_group_and_broadcast_pass_through() {
  Batcher b = make();
  Batcher::Frame f;
  b.add(3, 0, Command::Down, 0);
  TEST_ASSERT_TRUE(b.next(QUIET, f));
  assertFrame(3, 0, Command::Down, f);
  b.add(0, 0, Command::Stop, 1000);
  TEST_ASSERT_TRUE(b.next(1000 + QUIET, f));
  assertFrame(0, 0, Command::Stop, f);
}

static void test_clear_membership() {
  Batcher b = make();
  b.clearMembers();
  Batcher::Frame f;
  b.add(1, 1, Command::Up, 0);
  b.add(1, 2, Command::Up, 0);
  TEST_ASSERT_TRUE(b.next(QUIET, f));
  TEST_ASSERT_NOT_EQUAL(0, f.member);
}

static void test_ignores_out_of_range_addresses() {
  Batcher b = make();
  Batcher::Frame f;
  b.add(8, 1, Command::Up, 0);
  b.add(1, 8, Command::Up, 0);
  TEST_ASSERT_FALSE(b.next(QUIET, f));
}

static void test_deadline_survives_millis_wraparound() {
  Batcher b = make();
  Batcher::Frame f;
  const uint32_t t = 0xFFFFFFFFu - 50;
  b.add(1, 1, Command::Up, t);
  b.add(1, 2, Command::Up, t + 100);  // extends across the wrap
  TEST_ASSERT_FALSE(b.next(t + 100 + QUIET - 1, f));
  TEST_ASSERT_TRUE(b.next(t + 100 + QUIET, f));
}

static void test_reports_time_until_next_deadline() {
  Batcher b = make();
  TEST_ASSERT_FALSE(b.pending());
  b.add(1, 1, Command::Up, 1000);
  TEST_ASSERT_TRUE(b.pending());
  TEST_ASSERT_EQUAL_UINT32(QUIET, b.msUntilDue(1000));
  TEST_ASSERT_EQUAL_UINT32(QUIET - 100, b.msUntilDue(1100));
  b.add(2, 1, Command::Up, 1100);
  TEST_ASSERT_EQUAL_UINT32(QUIET, b.msUntilDue(1100));
  TEST_ASSERT_EQUAL_UINT32(0, b.msUntilDue(2000));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_single_command_waits_for_the_quiet_period);
  RUN_TEST(test_whole_room_becomes_one_group_frame);
  RUN_TEST(test_whole_house_becomes_one_broadcast);
  RUN_TEST(test_each_command_extends_the_quiet_period);
  RUN_TEST(test_gap_longer_than_quiet_period_splits_batches);
  RUN_TEST(test_steady_stream_leaves_at_maximum_wait);
  RUN_TEST(test_command_arriving_mid_flush_joins_without_waiting);
  RUN_TEST(test_incomplete_room_stays_individual);
  RUN_TEST(test_different_commands_do_not_merge);
  RUN_TEST(test_latest_command_per_shutter_wins);
  RUN_TEST(test_replaced_command_keeps_its_place_in_line);
  RUN_TEST(test_stop_goes_first);
  RUN_TEST(test_stop_arriving_mid_flush_jumps_ahead);
  RUN_TEST(test_whole_house_stop_is_one_frame);
  RUN_TEST(test_single_member_group_is_not_widened);
  RUN_TEST(test_without_membership_nothing_merges);
  RUN_TEST(test_explicit_group_and_broadcast_pass_through);
  RUN_TEST(test_clear_membership);
  RUN_TEST(test_ignores_out_of_range_addresses);
  RUN_TEST(test_deadline_survives_millis_wraparound);
  RUN_TEST(test_reports_time_until_next_deadline);
  return UNITY_END();
}
