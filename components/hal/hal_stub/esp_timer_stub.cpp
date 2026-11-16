// Implementation of the ESP-IDF `esp_timer` C API for the host (`linux`)
// target, on top of FreeRTOS software timers.
//
// ESP-IDF's own `esp_timer` component registers only its headers for the
// `linux` target (see `components/esp_timer/CMakeLists.txt`), so every
// `esp_timer_*` symbol is unresolved there. Linking `hal_stub` supplies them,
// which lets the same timer test suite run on the host that runs on the board.
//
// The public contract is the real one in `esp_timer.h`; this file only fills in
// the parts that need a platform. The differences from the chip implementation
// are deliberate and documented at each point:
//
//   * Time is `CLOCK_MONOTONIC`, not a hardware counter, so it has microsecond
//     resolution but an arbitrary origin. `esp_timer_get_time()` subtracts the
//     origin captured on first use, giving microseconds since that call.
//   * Arming goes through a FreeRTOS software timer, whose resolution is the
//     FreeRTOS tick (`configTICK_RATE_HZ`, 100 Hz in the host test config). A
//     request shorter than one tick is rounded up to one tick rather than
//     firing immediately, and any delay is rounded up to whole ticks.
//   * `ESP_TIMER_ISR` dispatch and the ETM alarm event are not available on the
//     host; `esp_timer_new_etm_alarm_event()` returns `ESP_ERR_NOT_SUPPORTED`.
//
// The API is thread-safe: all timer state is protected by one mutex. A user
// callback is invoked with the mutex released, so a callback may call back into
// the API (stop itself, restart itself, create another timer) without
// deadlocking.
// NOLINTBEGIN
#pragma GCC diagnostic push  // NOLINT(ignored diagnostics)
#include "esp_err.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#pragma GCC diagnostic pop
// NOLINTEND

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <new>
#include <string>
#include <vector>

// Every microsecond value below is a duration or a monotonic timestamp on that
// same scale. The public `esp_timer` API still uses `uint64_t`, so conversions
// happen only at its boundary.
using Microseconds = std::chrono::microseconds;

// The concrete definition of the opaque handle declared in `esp_timer.h`.
struct esp_timer {
    esp_timer_cb_t callback = nullptr;
    void* arg = nullptr;
    std::string name;
    TimerHandle_t rtos_timer = nullptr;
    bool periodic = false;
    bool armed = false;
    bool stopped = false;
    bool callback_running = false;
    bool skip_unhandled_events = false;
    // Period for a periodic timer; 0 for a one-shot timer.
    Microseconds period{0};
    // Monotonic time of the next alarm. For a disarmed one-shot it keeps the
    // last requested alarm time.
    Microseconds expiry{0};
    // Bumped on every arm/disarm so a periodic callback can tell whether the
    // timer was restarted or stopped while it was running.
    uint64_t generation = 0;
};

namespace {

// How long the API waits for the timer service task to accept a command.
// `portMAX_DELAY` matches the real implementation's blocking behaviour from a
// task; the non-blocking path is used only from the timer task itself.
constexpr TickType_t kCommandWait = portMAX_DELAY;

std::mutex g_mutex;
bool g_initialized = false;
std::vector<struct esp_timer*> g_timers;

// Monotonic origin for `esp_timer_get_time()`. The first call establishes it,
// so the returned value is small and relative, like microseconds since boot.
Microseconds now() {
    static const std::chrono::steady_clock::time_point origin =
        std::chrono::steady_clock::now();
    const auto elapsed = std::chrono::steady_clock::now() - origin;
    return std::chrono::duration_cast<Microseconds>(elapsed);
}

// How far an absolute alarm may already be in the past and still be accepted.
// The chip implementation tolerates its own scheduling overhead; the host stub
// needs the same slack because `esp_timer_start_once()` reads the clock twice,
// once to compute the alarm and once here.
constexpr Microseconds kPastTolerance{1000};

// The non-negative delay to arm, clamped when the alarm is within the tolerance
// above (and therefore marginally in the past).
Microseconds relative_delay(Microseconds alarm, Microseconds current) {
    return alarm > current ? alarm - current : Microseconds{0};
}

// Rounds a microsecond delay up to whole FreeRTOS ticks, with a minimum of one
// tick because a zero period is rejected by FreeRTOS.
TickType_t us_to_ticks(Microseconds delay) {
    // Round up: a positive delay shorter than one millisecond must still arm.
    const int64_t delay_ms =
        std::chrono::ceil<std::chrono::milliseconds>(delay).count();
    const int64_t rounded_ms = delay_ms == 0 ? 1 : delay_ms;
    TickType_t const ticks = pdMS_TO_TICKS(rounded_ms);
    return ticks == 0 ? 1 : ticks;
}

bool is_timer_service_task() {
    return xTaskGetCurrentTaskHandle() == xTimerGetTimerDaemonTaskHandle();
}

// Whether `handle` is one this library created and has not deleted yet.
bool is_known(esp_timer_handle_t handle) {
    return std::find(g_timers.begin(), g_timers.end(), handle) !=
           g_timers.end();
}

// Arms already-recorded state on the FreeRTOS timer. Called with the mutex
// released, because the timer service task may need it while it drains the
// command queue.
bool arm_rtos_timer(struct esp_timer* timer, Microseconds delay) {
    return xTimerChangePeriod(timer->rtos_timer, us_to_ticks(delay),
                              kCommandWait) == pdPASS;
}

}  // namespace

namespace {

void timer_callback(TimerHandle_t rtos_timer);

// Creates the FreeRTOS timer for a new handle. The timer is created disarmed
// with a one-tick period; every start changes the period.
bool create_rtos_timer(struct esp_timer* timer) {
    timer->rtos_timer =
        xTimerCreate("esp_timer", 1, pdFALSE, timer, &timer_callback);
    return timer->rtos_timer != nullptr;
}

// The callback FreeRTOS runs in the timer service task.
void timer_callback(TimerHandle_t rtos_timer) {
    auto* const timer =
        static_cast<struct esp_timer*>(pvTimerGetTimerID(rtos_timer));

    bool rearm = false;
    Microseconds period{0};
    {
        std::lock_guard<std::mutex> const lock(g_mutex);
        timer->callback_running = true;
        // Match the chip implementation, which queues a periodic timer's next
        // alarm *before* invoking the callback: `esp_timer_stop()` then works
        // from inside the callback, and `esp_timer_get_next_alarm()` already
        // reports the next expiry.
        if (timer->periodic && !timer->stopped) {
            ++timer->generation;
            timer->armed = true;
            timer->expiry = now() + timer->period;
            period = timer->period;
            rearm = true;
        } else {
            // A one-shot timer is disarmed once it fires.
            timer->armed = false;
        }
    }

    if (rearm) {
        // Non-blocking: this runs in the timer service task, which must not
        // wait for itself to drain the command queue.
        (void)xTimerChangePeriod(rtos_timer, us_to_ticks(period), 0);
    }

    timer->callback(timer->arg);

    {
        std::lock_guard<std::mutex> const lock(g_mutex);
        timer->callback_running = false;
    }
}

}  // namespace

extern "C" {

esp_err_t esp_timer_early_init(void) {
    // Nothing to do on the host: the time origin is established lazily and the
    // FreeRTOS timer service is started by the scheduler.
    return ESP_OK;
}

esp_err_t esp_timer_init(void) {
    std::lock_guard<std::mutex> const lock(g_mutex);
    if (g_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    g_initialized = true;
    return ESP_OK;
}

esp_err_t esp_timer_deinit(void) {
    std::lock_guard<std::mutex> const lock(g_mutex);
    if (!g_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    // Match the chip implementation: deinit is refused while any timer is
    // armed. Inactive handles may still exist and are freed by their owner.
    for (struct esp_timer* const timer : g_timers) {
        if (timer->armed) {
            return ESP_ERR_INVALID_STATE;
        }
    }
    g_initialized = false;
    return ESP_OK;
}

esp_err_t esp_timer_create(const esp_timer_create_args_t* create_args,
                           esp_timer_handle_t* out_handle) {
    if (!g_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (create_args == nullptr || create_args->callback == nullptr ||
        out_handle == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    if (create_args->dispatch_method < 0 ||
        create_args->dispatch_method >= ESP_TIMER_MAX) {
        return ESP_ERR_INVALID_ARG;
    }

    auto* const timer = new (std::nothrow) struct esp_timer;
    if (timer == nullptr) {
        return ESP_ERR_NO_MEM;
    }

    timer->callback = create_args->callback;
    timer->arg = create_args->arg;
    timer->name = create_args->name != nullptr ? create_args->name : "timer";
    timer->skip_unhandled_events = create_args->skip_unhandled_events;

    if (!create_rtos_timer(timer)) {
        delete timer;
        return ESP_ERR_NO_MEM;
    }

    {
        std::lock_guard<std::mutex> const lock(g_mutex);
        g_timers.push_back(timer);
    }
    *out_handle = timer;
    return ESP_OK;
}

esp_err_t esp_timer_start_once(esp_timer_handle_t timer, uint64_t timeout_us) {
    const Microseconds timeout{timeout_us};
    return esp_timer_start_once_at(
        timer, static_cast<uint64_t>((now() + timeout).count()));
}

esp_err_t esp_timer_start_once_at(esp_timer_handle_t timer, uint64_t alarm_us) {
    if (timer == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!g_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    const Microseconds alarm{alarm_us};
    const Microseconds current = now();
    if (alarm + kPastTolerance < current) {
        return ESP_ERR_INVALID_ARG;
    }

    {
        std::lock_guard<std::mutex> const lock(g_mutex);
        if (!is_known(timer)) {
            return ESP_ERR_INVALID_ARG;
        }
        if (timer->armed) {
            return ESP_ERR_INVALID_STATE;
        }
        timer->periodic = false;
        timer->period = Microseconds{0};
        timer->expiry = alarm;
        timer->armed = true;
        timer->stopped = false;
        ++timer->generation;
    }

    if (!arm_rtos_timer(timer, relative_delay(alarm, current))) {
        std::lock_guard<std::mutex> const lock(g_mutex);
        timer->armed = false;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t esp_timer_start_periodic(esp_timer_handle_t timer,
                                   uint64_t period_us) {
    // Clamp before computing the first alarm: a zero period would otherwise
    // make the alarm fall in the past between the two clock reads.
    const Microseconds period{period_us == 0 ? 1000 : period_us};
    return esp_timer_start_periodic_at(
        timer, static_cast<uint64_t>(period.count()),
        static_cast<uint64_t>((now() + period).count()));
}

// Mirrors ESP-IDF's `esp_timer_start_periodic_at` (`esp_timer.h`), which takes
// a period and a first-alarm time as two microsecond values in that fixed
// order. The stub implements the chip's API verbatim so the host tests and the
// on-target tests call the same signature.
esp_err_t esp_timer_start_periodic_at(esp_timer_handle_t timer,
                                      uint64_t period_us,
                                      uint64_t first_alarm_us) {
    if (timer == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!g_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    const Microseconds period{period_us == 0 ? 1000 : period_us};
    const Microseconds first_alarm{first_alarm_us};
    const Microseconds current = now();
    if (first_alarm + kPastTolerance < current) {
        return ESP_ERR_INVALID_ARG;
    }

    {
        std::lock_guard<std::mutex> const lock(g_mutex);
        if (!is_known(timer)) {
            return ESP_ERR_INVALID_ARG;
        }
        if (timer->armed) {
            return ESP_ERR_INVALID_STATE;
        }
        timer->periodic = true;
        timer->period = period;
        timer->expiry = first_alarm;
        timer->armed = true;
        timer->stopped = false;
        ++timer->generation;
    }

    if (!arm_rtos_timer(timer, relative_delay(first_alarm, current))) {
        std::lock_guard<std::mutex> const lock(g_mutex);
        timer->armed = false;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t esp_timer_restart(esp_timer_handle_t timer, uint64_t timeout_us) {
    const Microseconds timeout{timeout_us};
    return esp_timer_restart_at(
        timer, timeout_us, static_cast<uint64_t>((now() + timeout).count()));
}

// Mirrors ESP-IDF's `esp_timer_restart_at` (`esp_timer.h`) for the same
// reason: the two microsecond arguments, and their order, come from the chip's
// API rather than from this stub.
esp_err_t esp_timer_restart_at(esp_timer_handle_t timer, uint64_t period_us,
                               uint64_t first_alarm_us) {
    if (timer == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!g_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    const Microseconds first_alarm{first_alarm_us};
    const Microseconds current = now();
    if (first_alarm + kPastTolerance < current) {
        return ESP_ERR_INVALID_ARG;
    }

    {
        std::lock_guard<std::mutex> const lock(g_mutex);
        if (!is_known(timer)) {
            return ESP_ERR_INVALID_ARG;
        }
        if (!timer->armed) {
            return ESP_ERR_INVALID_STATE;
        }
        if (timer->periodic) {
            timer->period = Microseconds{period_us == 0 ? 1000 : period_us};
        }
        timer->expiry = first_alarm;
        timer->stopped = false;
        ++timer->generation;
    }

    if (!arm_rtos_timer(timer, relative_delay(first_alarm, current))) {
        std::lock_guard<std::mutex> const lock(g_mutex);
        timer->armed = false;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t esp_timer_stop(esp_timer_handle_t timer) {
    if (timer == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!g_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    {
        std::lock_guard<std::mutex> const lock(g_mutex);
        if (!is_known(timer)) {
            return ESP_ERR_INVALID_ARG;
        }
        if (!timer->armed) {
            return ESP_ERR_INVALID_STATE;
        }
        timer->armed = false;
        timer->stopped = true;
        ++timer->generation;
    }

    (void)xTimerStop(timer->rtos_timer, kCommandWait);
    return ESP_OK;
}

esp_err_t esp_timer_stop_blocking(esp_timer_handle_t timer,
                                  uint32_t timeout_ticks) {
    if (timer == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!g_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    bool callback_running = false;
    {
        std::lock_guard<std::mutex> const lock(g_mutex);
        if (!is_known(timer)) {
            return ESP_ERR_INVALID_ARG;
        }
        if (timer->armed) {
            timer->armed = false;
            timer->stopped = true;
            ++timer->generation;
        }
        callback_running = timer->callback_running;
        if (callback_running) {
            // The timer is disarmed while its callback runs, but a periodic
            // timer would re-arm after the callback unless it is also marked
            // stopped. The generation bump invalidates that pending re-arm.
            timer->stopped = true;
            ++timer->generation;
        }
    }

    (void)xTimerStop(timer->rtos_timer, kCommandWait);

    if (!callback_running) {
        return ESP_OK;
    }
    // A callback cannot wait for itself: the timer service task is the one
    // that would have to run it. Returning OK matches the chip implementation
    // for a TASK-dispatch timer stopped from its own callback.
    if (is_timer_service_task()) {
        return ESP_OK;
    }

    const TickType_t start = xTaskGetTickCount();
    for (;;) {
        {
            std::lock_guard<std::mutex> const lock(g_mutex);
            if (!timer->callback_running) {
                return ESP_OK;
            }
        }
        // `portMAX_DELAY` is wider than the uint32_t API parameter on the
        // linux port; compare the API's value so the expression stays the same
        // width on both targets.
        if (timeout_ticks != static_cast<uint32_t>(portMAX_DELAY) &&
            xTaskGetTickCount() - start >= timeout_ticks) {
            return ESP_ERR_TIMEOUT;
        }
        vTaskDelay(1);
    }
}

esp_err_t esp_timer_delete(esp_timer_handle_t timer) {
    if (timer == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    bool callback_running = false;
    {
        std::lock_guard<std::mutex> const lock(g_mutex);
        if (!is_known(timer)) {
            return ESP_ERR_INVALID_ARG;
        }
        if (timer->armed) {
            return ESP_ERR_INVALID_STATE;
        }
        callback_running = timer->callback_running;
    }

    // Freeing the handle while its callback still reads it would use freed
    // memory. Wait unless this *is* the callback; a self-delete from the timer
    // task has no way to return safely, so it is rejected.
    if (callback_running) {
        if (is_timer_service_task()) {
            return ESP_ERR_INVALID_STATE;
        }
        while (true) {
            std::lock_guard<std::mutex> const lock(g_mutex);
            if (!timer->callback_running) {
                break;
            }
            vTaskDelay(1);
        }
    }

    {
        std::lock_guard<std::mutex> const lock(g_mutex);
        g_timers.erase(std::remove(g_timers.begin(), g_timers.end(), timer),
                       g_timers.end());
    }
    (void)xTimerDelete(timer->rtos_timer, kCommandWait);
    delete timer;
    return ESP_OK;
}

int64_t esp_timer_get_time(void) {
    return static_cast<int64_t>(now().count());
}

int64_t esp_timer_get_next_alarm(void) {
    std::lock_guard<std::mutex> const lock(g_mutex);
    Microseconds next = Microseconds::max();
    for (struct esp_timer* const timer : g_timers) {
        if (timer->armed) {
            next = std::min(next, timer->expiry);
        }
    }
    return next == Microseconds::max() ? INT64_MAX
                                       : static_cast<int64_t>(next.count());
}

int64_t esp_timer_get_next_alarm_for_wake_up(void) {
    std::lock_guard<std::mutex> const lock(g_mutex);
    Microseconds next = Microseconds::max();
    for (struct esp_timer* const timer : g_timers) {
        if (timer->armed && !timer->skip_unhandled_events) {
            next = std::min(next, timer->expiry);
        }
    }
    return next == Microseconds::max() ? INT64_MAX
                                       : static_cast<int64_t>(next.count());
}

esp_err_t esp_timer_get_period(esp_timer_handle_t timer, uint64_t* period) {
    if (timer == nullptr || period == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    std::lock_guard<std::mutex> const lock(g_mutex);
    if (!is_known(timer)) {
        return ESP_ERR_INVALID_ARG;
    }
    *period =
        timer->periodic ? static_cast<uint64_t>(timer->period.count()) : 0;
    return ESP_OK;
}

esp_err_t esp_timer_get_expiry_time(esp_timer_handle_t timer,
                                    uint64_t* expiry) {
    if (timer == nullptr || expiry == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    std::lock_guard<std::mutex> const lock(g_mutex);
    if (!is_known(timer)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (timer->periodic) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    *expiry = static_cast<uint64_t>(timer->expiry.count());
    return ESP_OK;
}

esp_err_t esp_timer_dump(FILE* stream) {
    if (stream == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    std::lock_guard<std::mutex> const lock(g_mutex);
    (void)fprintf(stream, "Name              | Period      | Alarm\n");
    (void)fprintf(stream, "------------------|-------------|-------------\n");
    for (struct esp_timer* const timer : g_timers) {
        if (!timer->armed) {
            continue;
        }
        (void)fprintf(stream, "%-17s | %-11llu | %llu\n", timer->name.c_str(),
                      static_cast<unsigned long long>(
                          timer->periodic ? timer->period.count() : 0),
                      static_cast<unsigned long long>(timer->expiry.count()));
    }
    return ESP_OK;
}

// Reports whether the timer is armed or its callback is currently running.
//
// A timer stopped while its callback is still executing stays active until the
// callback returns, matching the chip implementation. A handle that was created
// but never started, or one that is not known to this library, is inactive.
bool esp_timer_is_active(esp_timer_handle_t timer) {
    if (timer == nullptr) {
        return false;
    }
    std::lock_guard<std::mutex> const lock(g_mutex);
    if (!is_known(timer)) {
        return false;
    }
    return timer->armed || timer->callback_running;
}

esp_err_t esp_timer_new_etm_alarm_event(esp_etm_event_handle_t* out_event) {
    if (out_event == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    // There is no silicon ETM on the host.
    return ESP_ERR_NOT_SUPPORTED;
}

#if CONFIG_ESP_TIMER_SUPPORTS_ISR_DISPATCH_METHOD
void esp_timer_isr_dispatch_need_yield(void) {
    // Callbacks are dispatched from the FreeRTOS timer task on the host, so
    // there is never an ISR context that could request a yield.
}
#endif

}  // extern "C"
