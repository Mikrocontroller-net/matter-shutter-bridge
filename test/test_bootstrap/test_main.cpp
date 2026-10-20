#include <unity.h>

// Proves the native test environment builds and runs; real tests live in
// test_fernotron/ and test_shutter/.

void setUp() {}
void tearDown() {}

static void test_native_env_runs() {
  TEST_ASSERT_TRUE(__cplusplus >= 201703L);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_native_env_runs);
  return UNITY_END();
}
