#include <unity.h>

#include <panel/panel.h>

using namespace panel;

static constexpr uint32_t TICK = 20;

void setUp() {}
void tearDown() {}

// Holds the button from `from` for `ms`, then releases it until the release
// has been debounced. Returns the action the release produced.
static Action press(Button &b, uint32_t from, uint32_t ms) {
  for (uint32_t t = from; t <= from + ms; t += TICK) {
    TEST_ASSERT_EQUAL(Action::None, b.update(true, t));
  }
  Action result = Action::None;
  for (uint32_t t = from + ms + TICK; t <= from + ms + RELEASE_DEBOUNCE_MS + 2 * TICK; t += TICK) {
    const Action a = b.update(false, t);
    if (a != Action::None) result = a;
  }
  return result;
}

static Status idle() {
  Status s;
  s.wifiConnected = true;
  s.airtimeBudgetMs = 360000;
  return s;
}

static void assertColor(Color expected, Color actual) {
  TEST_ASSERT_EQUAL_UINT8(expected.r, actual.r);
  TEST_ASSERT_EQUAL_UINT8(expected.g, actual.g);
  TEST_ASSERT_EQUAL_UINT8(expected.b, actual.b);
}

static void test_short_press_does_nothing() {
  Button b;
  TEST_ASSERT_EQUAL(Action::None, press(b, 1000, 500));
  TEST_ASSERT_EQUAL(Hold::None, b.hold());
}

static void test_hold_reports_what_a_release_would_do() {
  Button b;
  b.update(true, 0);
  TEST_ASSERT_EQUAL(Hold::Short, b.hold());
  b.update(true, PAIRING_HOLD_MS - 1);
  TEST_ASSERT_EQUAL(Hold::Short, b.hold());
  b.update(true, PAIRING_HOLD_MS);
  TEST_ASSERT_EQUAL(Hold::Pairing, b.hold());
  b.update(true, RESET_HOLD_MS);
  TEST_ASSERT_EQUAL(Hold::Reset, b.hold());
}

static void test_three_seconds_opens_pairing() {
  Button b;
  TEST_ASSERT_EQUAL(Action::OpenPairing, press(b, 0, PAIRING_HOLD_MS));
}

static void test_ten_seconds_factory_resets() {
  Button b;
  TEST_ASSERT_EQUAL(Action::FactoryReset, press(b, 0, RESET_HOLD_MS));
}

static void test_action_comes_only_after_release_is_debounced() {
  Button b;
  for (uint32_t t = 0; t <= PAIRING_HOLD_MS; t += TICK) b.update(true, t);
  TEST_ASSERT_EQUAL(Action::None, b.update(false, PAIRING_HOLD_MS + TICK));
  TEST_ASSERT_EQUAL(Hold::Pairing, b.hold());
  TEST_ASSERT_EQUAL(Action::OpenPairing, b.update(false, PAIRING_HOLD_MS + RELEASE_DEBOUNCE_MS));
  TEST_ASSERT_EQUAL(Hold::None, b.hold());
}

static void test_contact_bounce_does_not_restart_the_hold() {
  Button b;
  for (uint32_t t = 0; t < 2000; t += TICK) b.update(true, t);
  b.update(false, 2000);  // one bouncing sample
  b.update(true, 2020);
  for (uint32_t t = 2040; t <= PAIRING_HOLD_MS; t += TICK) b.update(true, t);
  TEST_ASSERT_EQUAL(Hold::Pairing, b.hold());
}

static void test_idle_is_dim_green() { assertColor({0, 3, 0}, color(idle(), Hold::None, 1000)); }

static void test_no_wifi_is_orange() {
  Status s = idle();
  s.wifiConnected = false;
  assertColor({48, 16, 0}, color(s, Hold::None, 1000));
}

static void test_pairing_window_pulses_blue() {
  Status s = idle();
  s.wifiConnected = false;  // a fresh stick has no Wi-Fi yet but should show pairing
  s.pairingOpen = true;
  const Color low = color(s, Hold::None, 0);
  const Color high = color(s, Hold::None, PULSE_PERIOD_MS / 2);
  TEST_ASSERT_EQUAL_UINT8(0, low.r);
  TEST_ASSERT_EQUAL_UINT8(0, low.g);
  TEST_ASSERT_TRUE(high.b > low.b);
  assertColor(low, color(s, Hold::None, PULSE_PERIOD_MS));
}

static void test_radio_frame_flashes_white() {
  Status s = idle();
  s.anyFrame = true;
  s.lastFrameMs = 5000;
  assertColor({48, 48, 48}, color(s, Hold::None, 5000));
  assertColor({48, 48, 48}, color(s, Hold::None, 5000 + FRAME_FLASH_MS - 1));
  assertColor({0, 3, 0}, color(s, Hold::None, 5000 + FRAME_FLASH_MS));
}

static void test_no_flash_before_the_first_frame() {
  Status s = idle();
  s.lastFrameMs = 0;
  assertColor({0, 3, 0}, color(s, Hold::None, 10));
}

static void test_airtime_above_80_percent_is_red() {
  Status s = idle();
  s.airtimeUsedMs = 288000 - 1;
  assertColor({0, 3, 0}, color(s, Hold::None, 1000));
  s.airtimeUsedMs = 288000;
  assertColor({32, 0, 0}, color(s, Hold::None, 1000));
}

static void test_button_hold_overrides_everything() {
  Status s = idle();
  s.anyFrame = true;
  s.lastFrameMs = 1000;
  s.pairingOpen = true;
  assertColor({16, 16, 16}, color(s, Hold::Short, 1000));
  assertColor({0, 0, 64}, color(s, Hold::Pairing, 1000));
}

static void test_reset_hold_blinks_red() {
  assertColor({64, 0, 0}, color(idle(), Hold::Reset, 0));
  assertColor({0, 0, 0}, color(idle(), Hold::Reset, 250));
  assertColor({64, 0, 0}, color(idle(), Hold::Reset, 500));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_short_press_does_nothing);
  RUN_TEST(test_hold_reports_what_a_release_would_do);
  RUN_TEST(test_three_seconds_opens_pairing);
  RUN_TEST(test_ten_seconds_factory_resets);
  RUN_TEST(test_action_comes_only_after_release_is_debounced);
  RUN_TEST(test_contact_bounce_does_not_restart_the_hold);
  RUN_TEST(test_idle_is_dim_green);
  RUN_TEST(test_no_wifi_is_orange);
  RUN_TEST(test_pairing_window_pulses_blue);
  RUN_TEST(test_radio_frame_flashes_white);
  RUN_TEST(test_no_flash_before_the_first_frame);
  RUN_TEST(test_airtime_above_80_percent_is_red);
  RUN_TEST(test_button_hold_overrides_everything);
  RUN_TEST(test_reset_hold_blinks_red);
  return UNITY_END();
}
