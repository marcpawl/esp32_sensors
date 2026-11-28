// Entry point for the on-target libespidfcpp tests.
//
// All cases come from `test-espidf-cpp-lib`, linked WHOLE_ARCHIVE. A board
// cannot report an exit status, so the result is printed as a sentinel for
// `test.py` to read.

#include <cstdio>

#include "unity.h"
#include "unity_test_runner.h"

// Unity requires these, even when empty.
void setUp(void) {}

void tearDown(void) {}

extern "C" void app_main(void)
{
    unity_run_all_tests();

    const int failures = static_cast<int>(Unity.TestFailures);
    std::printf("test-espidf-cpp-esp32: %d failure(s)\n", failures);
    std::printf("ESPIDF_CPP_ESP32_TESTS: %s\n",
                failures == 0 ? "PASS" : "FAIL");
    std::fflush(stdout);
}
