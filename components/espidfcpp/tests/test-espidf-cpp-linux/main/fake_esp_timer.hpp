// Recording fake for the `esp_timer` C API used by the `espidf_cpp::Timer`
// tests.
//
// ESP-IDF registers only the `esp_timer` headers for the `linux` target, so the
// test executable supplies the symbols itself. The fake records what the
// wrapper asked for and lets a test drive the query state; it does not fire
// callbacks.
#pragma once

// NOLINTBEGIN
#pragma GCC diagnostic push  // NOLINT(ignored diagnostics)
#include "esp_err.h"
#include "esp_timer.h"
#pragma GCC diagnostic pop
// NOLINTEND

#include <string>

#include <cstdint>

namespace fake_esp_timer {

struct State {
    int create_calls = 0;
    int delete_calls = 0;
    int start_once_calls = 0;
    int start_periodic_calls = 0;
    int restart_calls = 0;
    int stop_calls = 0;
    int stop_blocking_calls = 0;
    int is_active_calls = 0;
    int get_period_calls = 0;

    esp_timer_cb_t callback = nullptr;
    void* arg = nullptr;
    std::string name;
    bool skip_unhandled_events = false;

    std::uint64_t last_delay_us = 0;
    std::uint32_t last_timeout_ticks = 0;

    bool active = false;
    bool periodic = false;
    std::uint64_t period_us = 0;
    std::uint64_t expiry_us = 0;
};

State& state();

void reset();

}  // namespace fake_esp_timer
