// Host-based unit-test entry point for the `espidf_cpp` wrappers.
//
// Built for the ESP-IDF `linux` target and executed natively (no hardware).
// Unity discovers every TEST_CASE linked in via WHOLE_ARCHIVE and runs the
// menu non-interactively when stdin is not a TTY.
//
// Run with:
//   scripts/dev.py test-linux
#include "unity.h"
#include "unity_test_runner.h"

#include <cstdlib>

// Unity requires these, even when empty.
void setUp(void) {}

void tearDown(void) {}

extern "C" void app_main(void) {
    // `unity_run_all_tests()` runs every registered case (printing PASS/FAIL)
    // and returns; it does not terminate the process. Exit explicitly with a
    // non-zero status when any case failed so `idf.py`/CI/shell can detect it,
    // and so the host binary does not block waiting for further input.
    unity_run_all_tests();
    std::exit(Unity.TestFailures == 0 ? EXIT_SUCCESS : EXIT_FAILURE);
}
