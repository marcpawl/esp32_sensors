// Entry point for the on-target `esp_timer` tests.
//
// Runs on an ESP32-S3 development board against the real `esp_timer`. The test
// cases live in the shared `esp_timer_tests` component, so the host executable
// in `../stub` runs exactly the same suite against the `hal_stub`
// implementation.
//
// Flash and run with idf.py (inside the dev container):
//   cd components/hal/tests/esp_timer/board
//   idf.py -B build_esp32s3 build
//   idf.py -B build_esp32s3 -p $ESP32_PORT flash monitor

#include <cstdio>
#include <cstdlib>

#include "esp_err.h"
#include "esp_timer.h"
#include "unity.h"
#include "unity_test_runner.h"

// Unity requires these, even when empty.
void setUp(void) {}

void tearDown(void) {}

extern "C" void app_main(void) {
    // The system normally initializes esp_timer during startup; call it anyway
    // so the suite also states the intended precondition in one place.
    const esp_err_t early = esp_timer_early_init();
    if (early != ESP_OK) {
        std::printf("esp_timer_early_init failed: %d\n", early);
        std::abort();
    }

    // An already-initialized library is not an error.
    const esp_err_t init = esp_timer_init();
    if (init != ESP_OK && init != ESP_ERR_INVALID_STATE) {
        std::printf("esp_timer_init failed: %d\n", init);
        std::abort();
    }

    unity_run_all_tests();

    const int failures = static_cast<int>(Unity.TestFailures);
    std::printf("esp_timer board tests: %d failure(s)\n", failures);
    // Machine-readable end-of-run line for `test.py`, which has no other way to
    // learn the board's result. Keep it the last line the suite prints.
    std::printf("ESP_TIMER_BOARD_TESTS: %s\n", failures == 0 ? "PASS" : "FAIL");
    std::fflush(stdout);
}
