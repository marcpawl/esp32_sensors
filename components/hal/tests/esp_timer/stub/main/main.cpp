// Entry point for the host (`linux`) `esp_timer` tests.
//
// The timer functions come from `hal_stub`'s FreeRTOS-backed implementation;
// the test cases are the shared `esp_timer_tests` component, identical to the
// ones the ESP32-S3 executable runs against real hardware.
//
// Build and run with idf.py (inside the dev container):
//   cd components/hal/tests/esp_timer/stub
//   idf.py -B build_linux --preview set-target linux
//   idf.py -B build_linux build
//   ./build_linux/esp32_sensors_esp_timer_stub_tests.elf

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
    const esp_err_t early = esp_timer_early_init();
    if (early != ESP_OK) {
        std::printf("esp_timer_early_init failed: %d\n", early);
        std::exit(EXIT_FAILURE);
    }

    // The board's startup code has already initialized esp_timer; an
    // already-initialized library is not an error.
    const esp_err_t init = esp_timer_init();
    if (init != ESP_OK && init != ESP_ERR_INVALID_STATE) {
        std::printf("esp_timer_init failed: %d\n", init);
        std::exit(EXIT_FAILURE);
    }

    unity_run_all_tests();

    std::printf("esp_timer host tests: %d failure(s)\n",
                static_cast<int>(Unity.TestFailures));
    std::fflush(stdout);
    // Like the other host suites: a non-zero exit status lets the shell and CI
    // detect a failure, and exiting keeps the process from waiting on input.
    std::exit(Unity.TestFailures == 0 ? EXIT_SUCCESS : EXIT_FAILURE);
}
