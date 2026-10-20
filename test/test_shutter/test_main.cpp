#include <unity.h>

#include <shutter/controller.h>
#include <shutter/estimator.h>

using namespace shutter;

// Up and down deliberately differ so tests catch the wrong one being used.
static constexpr TravelTimes TIMES{20000, 16000};
static constexpr uint32_t MARGIN = 2000;

static Controller make(Flags flags = {}, uint16_t position = OPEN) {
  Config cfg;
  cfg.times = TIMES;
  cfg.flags = flags;
  cfg.endMarginMs = MARGIN;
  return Controller(cfg, position);
}

void setUp() {}
void tearDown() {}

// --- Estimator ---------------------------------------------------------------

static void test_estimator_starts_stopped_at_given_position() {
  Estimator e(TIMES, 4200);
  TEST_ASSERT_EQUAL_UINT16(4200, e.position(12345));
  TEST_ASSERT_FALSE(e.moving());
}

static void test_estimator_closing_uses_down_time() {
  Estimator e(TIMES, OPEN);
  e.start(Direction::Down, 1000);
  TEST_ASSERT_EQUAL_UINT16(5000, e.position(1000 + TIMES.downMs / 2));
  TEST_ASSERT_EQUAL_UINT16(CLOSED, e.position(1000 + TIMES.downMs));
  TEST_ASSERT_EQUAL_UINT16(CLOSED, e.position(1000 + TIMES.downMs * 3));  // clamps at the end
}

static void test_estimator_opening_uses_up_time() {
  Estimator e(TIMES, CLOSED);
  e.start(Direction::Up, 0);
  TEST_ASSERT_EQUAL_UINT16(7500, e.position(TIMES.upMs / 4));
  TEST_ASSERT_EQUAL_UINT16(OPEN, e.position(TIMES.upMs));
}

static void test_estimator_stop_freezes_position() {
  Estimator e(TIMES, OPEN);
  e.start(Direction::Down, 0);
  e.stop(TIMES.downMs / 4);
  TEST_ASSERT_FALSE(e.moving());
  TEST_ASSERT_EQUAL_UINT16(2500, e.position(TIMES.downMs * 2));
}

static void test_estimator_duration_between_positions() {
  Estimator e(TIMES, OPEN);
  TEST_ASSERT_EQUAL_UINT32(TIMES.downMs * 3 / 10, e.durationMs(OPEN, 3000));
  TEST_ASSERT_EQUAL_UINT32(TIMES.upMs / 2, e.durationMs(CLOSED, 5000));
}

static void test_estimator_survives_millis_wraparound() {
  Estimator e(TIMES, OPEN);
  const uint32_t start = 0xFFFFFFFFu - TIMES.downMs / 4 + 1;  // wraps a quarter of the way down
  e.start(Direction::Down, start);
  TEST_ASSERT_EQUAL_UINT16(5000, e.position(start + TIMES.downMs / 2));
}

// --- Controller: end targets -------------------------------------------------

static void test_close_sends_down_and_reports_moving() {
  Controller c = make();
  TEST_ASSERT_EQUAL(Action::Down, c.goTo(CLOSED, 0));
  TEST_ASSERT_TRUE(c.moving());
  TEST_ASSERT_EQUAL(Direction::Down, c.direction());
  TEST_ASSERT_EQUAL_UINT16(CLOSED, c.target());
}

static void test_close_resends_down_at_end_of_travel_then_stalls() {
  Controller c = make();
  c.goTo(CLOSED, 0);
  TEST_ASSERT_EQUAL(Action::None, c.tick(TIMES.downMs));  // estimate is at the end, margin not over yet
  TEST_ASSERT_TRUE(c.moving());
  TEST_ASSERT_EQUAL(Action::Down, c.tick(TIMES.downMs + MARGIN));
  TEST_ASSERT_FALSE(c.moving());
  TEST_ASSERT_EQUAL_UINT16(CLOSED, c.position(TIMES.downMs + MARGIN));
  TEST_ASSERT_EQUAL(Action::None, c.tick(TIMES.downMs + MARGIN + 1));  // re-sent only once
}

static void test_end_resend_can_be_disabled() {
  Config cfg;
  cfg.times = TIMES;
  cfg.endMarginMs = MARGIN;
  cfg.resendAtEnd = false;
  Controller c(cfg, OPEN);
  c.goTo(CLOSED, 0);
  TEST_ASSERT_EQUAL(Action::None, c.tick(TIMES.downMs + MARGIN));
  TEST_ASSERT_FALSE(c.moving());
}

static void test_open_from_half_takes_half_the_up_time() {
  Controller c = make({}, 5000);
  TEST_ASSERT_EQUAL(Action::Up, c.goTo(OPEN, 0));
  TEST_ASSERT_TRUE(c.moving());
  TEST_ASSERT_EQUAL(Action::None, c.tick(TIMES.upMs / 2 + MARGIN - 1));
  TEST_ASSERT_EQUAL(Action::Up, c.tick(TIMES.upMs / 2 + MARGIN));
  TEST_ASSERT_EQUAL_UINT16(OPEN, c.position(TIMES.upMs));
}

static void test_end_target_always_sends_even_when_estimate_is_there() {
  // A physical remote may have moved the shutter without us knowing.
  Controller c = make({}, OPEN);
  TEST_ASSERT_EQUAL(Action::Up, c.goTo(OPEN, 0));
}

// --- Controller: mid targets -------------------------------------------------

static void test_mid_target_down_sends_timed_stop() {
  Controller c = make();
  TEST_ASSERT_EQUAL(Action::Down, c.goTo(3000, 0));
  const uint32_t arrive = TIMES.downMs * 3 / 10;
  TEST_ASSERT_EQUAL(Action::None, c.tick(arrive - 1));
  TEST_ASSERT_EQUAL(Action::Stop, c.tick(arrive));
  TEST_ASSERT_FALSE(c.moving());
  TEST_ASSERT_EQUAL_UINT16(3000, c.position(arrive + 10000));
}

static void test_mid_target_up_uses_up_time() {
  Controller c = make({}, CLOSED);
  TEST_ASSERT_EQUAL(Action::Up, c.goTo(7000, 0));
  const uint32_t arrive = TIMES.upMs * 3 / 10;
  TEST_ASSERT_EQUAL(Action::None, c.tick(arrive - 1));
  TEST_ASSERT_EQUAL(Action::Stop, c.tick(arrive));
  TEST_ASSERT_EQUAL_UINT16(7000, c.position(arrive));
}

static void test_mid_target_at_current_position_does_nothing() {
  Controller c = make({}, 4000);
  TEST_ASSERT_EQUAL(Action::None, c.goTo(4000, 0));
  TEST_ASSERT_EQUAL(Action::None, c.goTo(4000 + DEADBAND - 1, 0));
  TEST_ASSERT_FALSE(c.moving());
}

static void test_new_target_replaces_pending_stop() {
  Controller c = make();
  c.goTo(3000, 0);
  // Before the first stop is due, ask for 6000 instead: still going down, later stop.
  TEST_ASSERT_EQUAL(Action::Down, c.goTo(6000, 1000));
  const uint32_t firstArrive = TIMES.downMs * 3 / 10;
  TEST_ASSERT_EQUAL(Action::None, c.tick(firstArrive));
  TEST_ASSERT_EQUAL(Action::Stop, c.tick(TIMES.downMs * 6 / 10));
  TEST_ASSERT_EQUAL_UINT16(6000, c.position(TIMES.downMs));
}

// --- Controller: stop and reversal -------------------------------------------

static void test_stop_mid_travel_freezes_estimate() {
  Controller c = make();
  c.goTo(CLOSED, 0);
  TEST_ASSERT_EQUAL(Action::Stop, c.stop(TIMES.downMs / 4));
  TEST_ASSERT_FALSE(c.moving());
  TEST_ASSERT_EQUAL_UINT16(2500, c.position(TIMES.downMs));
  TEST_ASSERT_EQUAL_UINT16(2500, c.target());
  TEST_ASSERT_EQUAL(Action::None, c.tick(TIMES.downMs * 2));  // no stale end re-send
}

static void test_stop_when_idle_still_sends_stop() {
  Controller c = make({}, 4000);
  TEST_ASSERT_EQUAL(Action::Stop, c.stop(0));
  TEST_ASSERT_EQUAL_UINT16(4000, c.position(0));
}

static void test_reversal_mid_travel_starts_from_current_estimate() {
  Controller c = make();
  c.goTo(CLOSED, 0);
  const uint32_t half = TIMES.downMs / 2;  // at 5000
  TEST_ASSERT_EQUAL(Action::Up, c.goTo(OPEN, half));
  TEST_ASSERT_EQUAL(Direction::Up, c.direction());
  TEST_ASSERT_EQUAL_UINT16(2500, c.position(half + TIMES.upMs / 4));
  TEST_ASSERT_EQUAL(Action::Up, c.tick(half + TIMES.upMs / 2 + MARGIN));
  TEST_ASSERT_EQUAL_UINT16(OPEN, c.position(half + TIMES.upMs / 2 + MARGIN));
}

static void test_reversal_to_mid_target() {
  Controller c = make();
  c.goTo(CLOSED, 0);
  const uint32_t at8000 = TIMES.downMs * 8 / 10;
  TEST_ASSERT_EQUAL(Action::Up, c.goTo(5000, at8000));
  TEST_ASSERT_EQUAL(Action::Stop, c.tick(at8000 + TIMES.upMs * 3 / 10));
  TEST_ASSERT_EQUAL_UINT16(5000, c.position(at8000 + TIMES.upMs));
}

// --- Controller: polarity flags ----------------------------------------------

static void test_invert_ends_swaps_open_and_close_only() {
  Flags f;
  f.invertEnds = true;
  Controller c = make(f, 5000);
  TEST_ASSERT_EQUAL(Action::Down, c.goTo(OPEN, 0));
  TEST_ASSERT_EQUAL_UINT16(CLOSED, c.target());
  TEST_ASSERT_EQUAL(Action::Up, c.goTo(CLOSED, 1));
  TEST_ASSERT_EQUAL_UINT16(OPEN, c.target());
  // Mid targets unaffected.
  Controller d = make(f, OPEN);
  TEST_ASSERT_EQUAL(Action::Down, d.goTo(3000, 0));
  TEST_ASSERT_EQUAL_UINT16(3000, d.target());
}

static void test_invert_mid_mirrors_mid_targets_only() {
  Flags f;
  f.invertMid = true;
  Controller c = make(f, OPEN);
  TEST_ASSERT_EQUAL(Action::Down, c.goTo(3000, 0));
  TEST_ASSERT_EQUAL_UINT16(7000, c.target());
  Controller d = make(f, 5000);
  TEST_ASSERT_EQUAL(Action::Down, d.goTo(CLOSED, 0));
  TEST_ASSERT_EQUAL_UINT16(CLOSED, d.target());
}

static void test_out_of_range_target_is_clamped() {
  Controller c = make();
  TEST_ASSERT_EQUAL(Action::Down, c.goTo(12000, 0));
  TEST_ASSERT_EQUAL_UINT16(CLOSED, c.target());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_estimator_starts_stopped_at_given_position);
  RUN_TEST(test_estimator_closing_uses_down_time);
  RUN_TEST(test_estimator_opening_uses_up_time);
  RUN_TEST(test_estimator_stop_freezes_position);
  RUN_TEST(test_estimator_duration_between_positions);
  RUN_TEST(test_estimator_survives_millis_wraparound);
  RUN_TEST(test_close_sends_down_and_reports_moving);
  RUN_TEST(test_close_resends_down_at_end_of_travel_then_stalls);
  RUN_TEST(test_end_resend_can_be_disabled);
  RUN_TEST(test_open_from_half_takes_half_the_up_time);
  RUN_TEST(test_end_target_always_sends_even_when_estimate_is_there);
  RUN_TEST(test_mid_target_down_sends_timed_stop);
  RUN_TEST(test_mid_target_up_uses_up_time);
  RUN_TEST(test_mid_target_at_current_position_does_nothing);
  RUN_TEST(test_new_target_replaces_pending_stop);
  RUN_TEST(test_stop_mid_travel_freezes_estimate);
  RUN_TEST(test_stop_when_idle_still_sends_stop);
  RUN_TEST(test_reversal_mid_travel_starts_from_current_estimate);
  RUN_TEST(test_reversal_to_mid_target);
  RUN_TEST(test_invert_ends_swaps_open_and_close_only);
  RUN_TEST(test_invert_mid_mirrors_mid_targets_only);
  RUN_TEST(test_out_of_range_target_is_clamped);
  return UNITY_END();
}
