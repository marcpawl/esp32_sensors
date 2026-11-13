// Tests for the ESP-IDF restart path on the `linux` target.
//
// The firmware's reboot path ends in `esp_restart()`, so the host tests need to
// know that the call is available there and follows its contract. On `linux`
// `esp_restart()` runs the registered shutdown handlers and then exits the
// process, which a test runner cannot survive directly. Registering a handler
// that longjmps back out of the restart is the seam ESP-IDF's own `linux`
// `esp_system` tests use: the test observes that the shutdown path ran and that
// `esp_restart()` did not return normally, without ending the runner.
// NOLINTBEGIN
#pragma GCC diagnostic push  // NOLINT(ignored diagnostics)
#include "esp_system.h"
#pragma GCC diagnostic pop

// NOLINTEND

#include <csetjmp>

#include "unity.h"

namespace {

std::jmp_buf g_restart_jump;
bool g_shutdown_handler_ran = false;

// Registered as a shutdown handler: `esp_restart()` calls it before it tears
// the system down, and the longjmp returns control to the test case.
void jump_back_from_restart() {
    g_shutdown_handler_ran = true;
    std::longjmp(g_restart_jump, 1);
}

}  // namespace

TEST_CASE("esp_system: esp_restart runs the shutdown path on linux",
          "[esp_system]") {
    TEST_ASSERT_EQUAL(ESP_OK,
                      esp_register_shutdown_handler(jump_back_from_restart));

    g_shutdown_handler_ran = false;
    bool returned = false;

    if (setjmp(g_restart_jump) == 0) {
        esp_restart();
        // Reached only if the restart path did nothing: on `linux` it exits
        // the process after running the shutdown handlers.
        returned = true;
    }

    TEST_ASSERT_EQUAL(ESP_OK,
                      esp_unregister_shutdown_handler(jump_back_from_restart));
    TEST_ASSERT_FALSE_MESSAGE(returned, "esp_restart() returned");
    TEST_ASSERT_TRUE_MESSAGE(g_shutdown_handler_ran,
                             "esp_restart() skipped the shutdown handlers");
}
