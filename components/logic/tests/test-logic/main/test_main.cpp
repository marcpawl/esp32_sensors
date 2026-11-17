// Host-based unit-test entry point.
//
// Built for the ESP-IDF `linux` target and executed natively (no hardware).
// Unity discovers every TEST_CASE linked in via WHOLE_ARCHIVE and runs the
// menu non-interactively when stdin is not a TTY.
//
// Run with:
//   scripts/dev.py test-logic     (or `test`, which runs every suite)
// or directly in the container:
//   docker compose run --rm dev bash -lc 'cd test && idf.py --preview
//   -B build_linux set-target linux && idf.py -B build_linux build &&
//   ./build_linux/esp32_sensors_tests.elf'
// NOLINTBEGIN
#pragma GCC diagnostic push  // NOLINT(ignored diagnostics)
#include "esp_err.h"
#include "esp_timer.h"
#pragma GCC diagnostic pop
// NOLINTEND

#include <cstdio>
#include <cstdlib>

#include "unity.h"
#include "unity_test_runner.h"

// Unity requires these, even when empty.
void setUp(void) {}

void tearDown(void) {}

extern "C" void app_main(void) {
    // The host `esp_timer` implementation starts uninitialized; the board's
    // startup code does this on the chip. `test_toggle_switch.cpp` creates
    // `espidf_cpp::Timer`s, so initialize the library once for the suite.
    const esp_err_t early = esp_timer_early_init();
    if (early != ESP_OK) {
        std::printf("esp_timer_early_init failed: %d\n", early);
        std::exit(EXIT_FAILURE);
    }
    const esp_err_t init = esp_timer_init();
    if (init != ESP_OK && init != ESP_ERR_INVALID_STATE) {
        std::printf("esp_timer_init failed: %d\n", init);
        std::exit(EXIT_FAILURE);
    }

    // `unity_run_all_tests()` runs every registered case (printing PASS/FAIL)
    // and returns; it does not terminate the process. Exit explicitly with a
    // non-zero status when any case failed so `idf.py`/CI/shell can detect it,
    // and so the host binary does not block waiting for further input.
    unity_run_all_tests();
    std::exit(Unity.TestFailures == 0 ? EXIT_SUCCESS : EXIT_FAILURE);
}
