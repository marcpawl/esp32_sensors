// Tests for the `espidf_cpp::vTaskDelay` wrapper.
//
// The wrapper converts a `std::chrono::milliseconds` duration to FreeRTOS
// ticks and forwards it to FreeRTOS's `vTaskDelay()`. On the ESP-IDF `linux`
// target this exercises the real scheduling wrapper, so the cases use short
// delays.

#include <chrono>
#include <cstdio>

#include "espidf_cpp/task.hpp"
#include "unity.h"

TEST_CASE("espidf_cpp task: vTaskDelay accepts a zero delay",
          "[espidf_cpp_task]") {
    thermo::espidf_cpp::vTaskDelay(std::chrono::milliseconds{0});
    TEST_ASSERT_TRUE(true);
}

TEST_CASE("espidf_cpp task: vTaskDelay accepts a non-zero delay",
          "[espidf_cpp_task]") {
    thermo::espidf_cpp::vTaskDelay(std::chrono::milliseconds{1});
    TEST_ASSERT_TRUE(true);
}

TEST_CASE("espidf_cpp task: vTaskDelay delays", "[espidf_cpp_task]") {
    constexpr auto delay = std::chrono::milliseconds{500};
    auto const before = std::chrono::steady_clock::now();
    thermo::espidf_cpp::vTaskDelay(delay);
    auto const after = std::chrono::steady_clock::now();
    auto const actual_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(after - before)
            .count();
    if (actual_ms < delay.count()) {
        char message[96];
        std::snprintf(message, sizeof(message),
                      "expected >= %lld ms, actual %lld ms",
                      static_cast<long long>(delay.count()),
                      static_cast<long long>(actual_ms));
        TEST_FAIL_MESSAGE(message);
    }
}
