// Recording implementation of the `esp_timer` C API for the host tests.
#include "fake_esp_timer.hpp"

// NOLINTBEGIN
#pragma GCC diagnostic push  // NOLINT(ignored diagnostics)
#include "esp_err.h"
#include "esp_timer.h"
#pragma GCC diagnostic pop
// NOLINTEND

#include <new>

#include <cstdint>

namespace {

fake_esp_timer::State g_state;

}  // namespace

// The opaque handle declared by `esp_timer.h`.
struct esp_timer {
    int marker = 0;
};

namespace fake_esp_timer {

State& state() {
    return g_state;
}

void reset() {
    g_state = State{};
}

}  // namespace fake_esp_timer

extern "C" {

esp_err_t esp_timer_create(const esp_timer_create_args_t* create_args,
                           esp_timer_handle_t* out_handle) {
    if (create_args == nullptr || create_args->callback == nullptr ||
        out_handle == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    auto* const timer = new (std::nothrow) esp_timer;
    if (timer == nullptr) {
        return ESP_ERR_NO_MEM;
    }

    ++g_state.create_calls;
    g_state.callback = create_args->callback;
    g_state.arg = create_args->arg;
    g_state.name = create_args->name != nullptr ? create_args->name : "";
    g_state.skip_unhandled_events = create_args->skip_unhandled_events;
    *out_handle = timer;
    return ESP_OK;
}

esp_err_t esp_timer_delete(esp_timer_handle_t timer) {
    if (timer == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    if (g_state.active) {
        return ESP_ERR_INVALID_STATE;
    }
    ++g_state.delete_calls;
    delete timer;
    return ESP_OK;
}

esp_err_t esp_timer_start_once(esp_timer_handle_t timer, uint64_t timeout_us) {
    if (timer == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    if (g_state.active) {
        return ESP_ERR_INVALID_STATE;
    }
    ++g_state.start_once_calls;
    g_state.last_delay_us = timeout_us;
    g_state.active = true;
    g_state.periodic = false;
    g_state.period_us = 0;
    g_state.expiry_us = timeout_us;
    return ESP_OK;
}

esp_err_t esp_timer_start_periodic(esp_timer_handle_t timer,
                                   uint64_t period_us) {
    if (timer == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    if (g_state.active) {
        return ESP_ERR_INVALID_STATE;
    }
    ++g_state.start_periodic_calls;
    g_state.last_delay_us = period_us;
    g_state.active = true;
    g_state.periodic = true;
    g_state.period_us = period_us;
    g_state.expiry_us = period_us;
    return ESP_OK;
}

esp_err_t esp_timer_restart(esp_timer_handle_t timer, uint64_t timeout_us) {
    if (timer == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!g_state.active) {
        return ESP_ERR_INVALID_STATE;
    }
    ++g_state.restart_calls;
    g_state.last_delay_us = timeout_us;
    if (g_state.periodic) {
        g_state.period_us = timeout_us;
    }
    g_state.expiry_us = timeout_us;
    return ESP_OK;
}

esp_err_t esp_timer_stop(esp_timer_handle_t timer) {
    if (timer == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!g_state.active) {
        return ESP_ERR_INVALID_STATE;
    }
    ++g_state.stop_calls;
    g_state.active = false;
    return ESP_OK;
}

esp_err_t esp_timer_stop_blocking(esp_timer_handle_t timer,
                                  uint32_t timeout_ticks) {
    if (timer == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    ++g_state.stop_blocking_calls;
    g_state.last_timeout_ticks = timeout_ticks;
    g_state.active = false;
    return ESP_OK;
}

bool esp_timer_is_active(esp_timer_handle_t timer) {
    ++g_state.is_active_calls;
    return timer != nullptr && g_state.active;
}

esp_err_t esp_timer_get_period(esp_timer_handle_t timer, uint64_t* period) {
    ++g_state.get_period_calls;
    if (timer == nullptr || period == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    *period = g_state.periodic ? g_state.period_us : 0;
    return ESP_OK;
}

esp_err_t esp_timer_get_expiry_time(esp_timer_handle_t timer,
                                    uint64_t* expiry) {
    if (timer == nullptr || expiry == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    if (g_state.periodic) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    *expiry = g_state.expiry_us;
    return ESP_OK;
}

}  // extern "C"
