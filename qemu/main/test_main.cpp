// QEMU on-target unit-test entry point.
//
// Built for the ESP32-S3 target and executed under the ESP-IDF QEMU fork
// (`idf.py qemu` -> `qemu-system-xtensa -M esp32s3`). Unity discovers every
// TEST_CASE linked in via WHOLE_ARCHIVE and runs the menu non-interactively.
//
// Unlike the host suite (`test/`, which runs on the `linux` target), this
// project runs against the *real* FreeRTOS scheduler and the ESP-IDF runtime,
// so it covers code that cannot be host-tested: `thermo::Task` really creates
// a FreeRTOS task. The HAL cases here are linkage checks: they construct the
// pin-free `hal_stub` devices to prove the stub HAL links and works on-target,
// while the deeper device behaviour lives in the host suite.
//
// Run with:
//   scripts/dev.py test-qemu
#include "unity.h"
#include "unity_test_runner.h"

#include <cstdio>

// Unity requires these, even when empty.
void setUp(void) {}

void tearDown(void) {}

extern "C" void app_main(void) {
    // `unity_run_all_tests()` runs every registered case (printing PASS/FAIL)
    // and returns; it does not terminate the process.
    unity_run_all_tests();

    // Print a single unambiguous marker the QEMU driver greps for. A non-zero
    // failure count means the driver must fail even though QEMU itself exited
    // cleanly.
    std::printf("\nQEMU-TESTS-COMPLETE failures=%d\n", Unity.TestFailures);
    std::fflush(stdout);
}
