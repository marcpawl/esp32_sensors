// Native (linux-target) unit-test entry point.
//
// Built for the ESP-IDF `linux` target and run as a native ELF (no emulator).
// Unity discovers every TEST_CASE linked in via WHOLE_ARCHIVE and runs the menu
// non-interactively.
//
// Unlike the host suite (`test/`), this project runs against ESP-IDF's *real*
// FreeRTOS (pthread) scheduler, so it covers code the host suite cannot drive
// as hard: `thermo::Task` really creates a FreeRTOS task. The HAL cases here
// are linkage checks: they construct the pin-free `hal_stub` devices to prove
// the stub HAL links and works natively, while the deeper device behaviour
// lives in the host suite.
//
// Run with:
//   scripts/dev.py test-espidf-cpp
#include "unity.h"
#include "unity_test_runner.h"

#include <cstdio>

#include "esp_err.h"
#include "esp_event.h"

// Unity requires these, even when empty.
void setUp(void) {}

void tearDown(void) {}

extern "C" void app_main(void) {
    // Create the default event loop before the suite runs: the ESP-IDF HTTP
    // server (used by the `HttpsServer` cases to stand up a real plain-HTTP
    // listener on the linux target) posts its lifecycle events to it.
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // `unity_run_all_tests()` runs every registered case (printing PASS/FAIL)
    // and returns; it does not terminate the process.
    unity_run_all_tests();

    // Print a single unambiguous marker the test driver greps for. A non-zero
    // failure count means the driver must fail even though the process itself
    // exited cleanly.
    std::printf("\nESPIDF_CPP_TEST-COMPLETE failures=%lu\n",
                static_cast<unsigned long>(Unity.TestFailures));
    std::fflush(stdout);
}
