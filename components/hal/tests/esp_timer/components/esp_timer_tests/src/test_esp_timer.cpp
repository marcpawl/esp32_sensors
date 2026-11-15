// Shared test cases for the ESP-IDF `esp_timer` C API.
//
// The same cases run twice:
//
//   * on an ESP32-S3 development board, against the real `esp_timer`; and
//   * on the host (`linux`), against the implementation in `hal_stub`, which
//     arms FreeRTOS software timers.
//
// The cases therefore stay within what both can honour. Delays are tens of
// milliseconds and every wait is bounded, because the host stub is limited by
// the FreeRTOS tick (10 ms in the test configuration) while the chip timer is
// microsecond-accurate. Nothing asserts on wall-clock precision: the checks are
// "fired", "fired once", "fired at least twice", and "stopped".
//
// Initialization is the executable's job: its `app_main` calls
// `esp_timer_early_init()` and `esp_timer_init()` (tolerating an already
// initialized library) before `unity_run_all_tests()`. The lifecycle cases here
// only exercise the calls that can safely run after that.
// NOLINTBEGIN
#pragma GCC diagnostic push  // NOLINT(ignored diagnostics)
#include "esp_err.h"
#include "esp_etm.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#pragma GCC diagnostic pop
// NOLINTEND

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "unity.h"

namespace {

// Long enough to sit well above a FreeRTOS tick, short enough to keep the
// suite quick. Every wait is bounded by `kTimeoutMs`.
constexpr uint64_t kShortUs = 50'000;
constexpr uint64_t kMediumUs = 150'000;
constexpr uint64_t kLongUs = 500'000;
constexpr int kTimeoutMs = 3000;
// `portMAX_DELAY` is 64-bit on the linux port while esp_timer takes uint32_t;
// the chip implementation's value is UINT32_MAX.
constexpr uint32_t kForever = UINT32_MAX;

std::atomic<int> g_calls{0};
std::atomic<bool> g_callback_started{false};
std::atomic<bool> g_callback_finished{false};
std::atomic<int> g_stop_result{0};
esp_timer_handle_t g_self_handle = nullptr;

void count_cb(void* arg) {
    static_cast<std::atomic<int>*>(arg)->fetch_add(1);
}

void slow_cb(void* /*arg*/) {
    g_callback_started.store(true);
    vTaskDelay(pdMS_TO_TICKS(100));
    g_callback_finished.store(true);
    g_calls.fetch_add(1);
}

void self_stop_cb(void* /*arg*/) {
    // Stopping the timer from its own callback must not deadlock.
    g_stop_result.store(
        static_cast<int>(esp_timer_stop_blocking(g_self_handle, kForever)));
    g_calls.fetch_add(1);
}

void reset_calls() {
    g_calls.store(0);
    g_callback_started.store(false);
    g_callback_finished.store(false);
    g_stop_result.store(0);
    g_self_handle = nullptr;
}

esp_timer_handle_t create_timer(esp_timer_cb_t callback = &count_cb,
                                void* arg = &g_calls,
                                bool skip_unhandled_events = false) {
    esp_timer_create_args_t args = {};
    args.callback = callback;
    args.arg = arg;
    args.dispatch_method = ESP_TIMER_TASK;
    args.name = "test-timer";
    args.skip_unhandled_events = skip_unhandled_events;
    esp_timer_handle_t handle = nullptr;
    if (esp_timer_create(&args, &handle) != ESP_OK) {
        return nullptr;
    }
    return handle;
}

// Stops (if needed) and deletes a timer. Safe on an expired one-shot.
void stop_and_delete(esp_timer_handle_t handle) {
    if (handle == nullptr) {
        return;
    }
    (void)esp_timer_stop_blocking(handle, kForever);
    TEST_ASSERT_EQUAL(ESP_OK, esp_timer_delete(handle));
}

bool wait_for_calls(int count) {
    for (int waited = 0; waited < kTimeoutMs; waited += 10) {
        if (g_calls.load() >= count) {
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    return g_calls.load() >= count;
}

bool wait_for_inactive(esp_timer_handle_t handle) {
    for (int waited = 0; waited < kTimeoutMs; waited += 10) {
        if (!esp_timer_is_active(handle)) {
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    return !esp_timer_is_active(handle);
}

}  // namespace

// --- Initialization ---------------------------------------------------------

TEST_CASE("esp_timer: early init returns ESP_OK", "[esp_timer]") {
    TEST_ASSERT_EQUAL(ESP_OK, esp_timer_early_init());
}

TEST_CASE("esp_timer: init on an initialized library is harmless",
          "[esp_timer]") {
    // The executable initializes the library; the chip implementation reports
    // the duplicate, while a stub may accept it. Both outcomes are valid.
    const esp_err_t err = esp_timer_init();
    TEST_ASSERT_TRUE(err == ESP_OK || err == ESP_ERR_INVALID_STATE);
}

// --- Creation ---------------------------------------------------------------

TEST_CASE("esp_timer: create rejects null arguments", "[esp_timer]") {
    esp_timer_handle_t handle = nullptr;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, esp_timer_create(nullptr, &handle));

    esp_timer_create_args_t args = {};
    args.callback = &count_cb;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, esp_timer_create(&args, nullptr));

    args.callback = nullptr;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, esp_timer_create(&args, &handle));
}

TEST_CASE("esp_timer: create returns a usable, inactive handle",
          "[esp_timer]") {
    esp_timer_handle_t handle = create_timer();
    TEST_ASSERT_NOT_NULL(handle);
    TEST_ASSERT_FALSE(esp_timer_is_active(handle));
    TEST_ASSERT_EQUAL(ESP_OK, esp_timer_delete(handle));
}

// --- One-shot timers --------------------------------------------------------

TEST_CASE("esp_timer: start_once fires exactly once", "[esp_timer]") {
    reset_calls();
    esp_timer_handle_t handle = create_timer();
    TEST_ASSERT_NOT_NULL(handle);

    TEST_ASSERT_EQUAL(ESP_OK, esp_timer_start_once(handle, kShortUs));
    TEST_ASSERT_TRUE(esp_timer_is_active(handle));
    TEST_ASSERT_TRUE(wait_for_calls(1));
    TEST_ASSERT_TRUE(wait_for_inactive(handle));

    // Many more periods must not produce another callback.
    vTaskDelay(pdMS_TO_TICKS(3 * kShortUs / 1000));
    TEST_ASSERT_EQUAL(1, g_calls.load());
    stop_and_delete(handle);
}

TEST_CASE("esp_timer: start_once while armed is rejected", "[esp_timer]") {
    reset_calls();
    esp_timer_handle_t handle = create_timer();
    TEST_ASSERT_NOT_NULL(handle);

    TEST_ASSERT_EQUAL(ESP_OK, esp_timer_start_once(handle, kLongUs));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE,
                      esp_timer_start_once(handle, kLongUs));
    stop_and_delete(handle);
}

TEST_CASE("esp_timer: start_once_at uses the absolute alarm", "[esp_timer]") {
    reset_calls();
    esp_timer_handle_t handle = create_timer();
    TEST_ASSERT_NOT_NULL(handle);

    const int64_t now = esp_timer_get_time();
    TEST_ASSERT_EQUAL(
        ESP_OK,
        esp_timer_start_once_at(handle, static_cast<uint64_t>(now) + kShortUs));
    TEST_ASSERT_TRUE(wait_for_calls(1));
    TEST_ASSERT_EQUAL(1, g_calls.load());
    stop_and_delete(handle);
}

TEST_CASE("esp_timer: start_once_at rejects an alarm in the past",
          "[esp_timer]") {
    esp_timer_handle_t handle = create_timer();
    TEST_ASSERT_NOT_NULL(handle);

    // Zero is before any monotonic time base, so it is always in the past.
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, esp_timer_start_once_at(handle, 0));
    stop_and_delete(handle);
}

// --- Periodic timers --------------------------------------------------------

TEST_CASE("esp_timer: start_periodic fires repeatedly", "[esp_timer]") {
    reset_calls();
    esp_timer_handle_t handle = create_timer();
    TEST_ASSERT_NOT_NULL(handle);

    TEST_ASSERT_EQUAL(ESP_OK, esp_timer_start_periodic(handle, kShortUs));
    TEST_ASSERT_TRUE(wait_for_calls(2));
    TEST_ASSERT_EQUAL(ESP_OK, esp_timer_stop_blocking(handle, kForever));

    // Once stopped, no further callback may run.
    const int after_stop = g_calls.load();
    vTaskDelay(pdMS_TO_TICKS(3 * kShortUs / 1000));
    TEST_ASSERT_EQUAL(after_stop, g_calls.load());
    stop_and_delete(handle);
}

TEST_CASE("esp_timer: start_once accepts a zero timeout", "[esp_timer]") {
    reset_calls();
    esp_timer_handle_t handle = create_timer();
    TEST_ASSERT_NOT_NULL(handle);
    TEST_ASSERT_EQUAL(ESP_OK, esp_timer_start_once(handle, 0));
    TEST_ASSERT_TRUE(wait_for_calls(1));
    stop_and_delete(handle);
}

TEST_CASE("esp_timer: start_periodic_at honours the first alarm",
          "[esp_timer]") {
    reset_calls();
    esp_timer_handle_t handle = create_timer();
    TEST_ASSERT_NOT_NULL(handle);

    const int64_t now = esp_timer_get_time();
    TEST_ASSERT_EQUAL(
        ESP_OK, esp_timer_start_periodic_at(
                    handle, kShortUs, static_cast<uint64_t>(now) + kShortUs));
    TEST_ASSERT_TRUE(wait_for_calls(2));
    TEST_ASSERT_EQUAL(ESP_OK, esp_timer_stop_blocking(handle, kForever));
    stop_and_delete(handle);
}

TEST_CASE("esp_timer: restart changes a one-shot deadline", "[esp_timer]") {
    reset_calls();
    esp_timer_handle_t handle = create_timer();
    TEST_ASSERT_NOT_NULL(handle);

    TEST_ASSERT_EQUAL(ESP_OK, esp_timer_start_once(handle, kLongUs));
    TEST_ASSERT_EQUAL(ESP_OK, esp_timer_restart(handle, kShortUs));
    TEST_ASSERT_TRUE(wait_for_calls(1));
    vTaskDelay(pdMS_TO_TICKS(3 * kLongUs / 1000));
    TEST_ASSERT_EQUAL(1, g_calls.load());
    stop_and_delete(handle);
}

TEST_CASE("esp_timer: restart changes a periodic period", "[esp_timer]") {
    reset_calls();
    esp_timer_handle_t handle = create_timer();
    TEST_ASSERT_NOT_NULL(handle);

    TEST_ASSERT_EQUAL(ESP_OK, esp_timer_start_periodic(handle, kLongUs));
    TEST_ASSERT_TRUE(wait_for_calls(1));
    TEST_ASSERT_EQUAL(ESP_OK, esp_timer_restart(handle, kShortUs));
    TEST_ASSERT_TRUE(wait_for_calls(3));
    TEST_ASSERT_EQUAL(ESP_OK, esp_timer_stop_blocking(handle, kForever));
    stop_and_delete(handle);
}

TEST_CASE("esp_timer: restart on an inactive timer is rejected",
          "[esp_timer]") {
    esp_timer_handle_t handle = create_timer();
    TEST_ASSERT_NOT_NULL(handle);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE,
                      esp_timer_restart(handle, kShortUs));
    stop_and_delete(handle);
}

TEST_CASE("esp_timer: restart_at uses the absolute alarm", "[esp_timer]") {
    reset_calls();
    esp_timer_handle_t handle = create_timer();
    TEST_ASSERT_NOT_NULL(handle);

    TEST_ASSERT_EQUAL(ESP_OK, esp_timer_start_once(handle, kLongUs));
    const int64_t now = esp_timer_get_time();
    TEST_ASSERT_EQUAL(
        ESP_OK,
        esp_timer_restart_at(handle, 0, static_cast<uint64_t>(now) + kShortUs));
    TEST_ASSERT_TRUE(wait_for_calls(1));
    vTaskDelay(pdMS_TO_TICKS(3 * kLongUs / 1000));
    TEST_ASSERT_EQUAL(1, g_calls.load());
    stop_and_delete(handle);
}

// --- Stopping ---------------------------------------------------------------

TEST_CASE("esp_timer: stop disarms a running timer", "[esp_timer]") {
    reset_calls();
    esp_timer_handle_t handle = create_timer();
    TEST_ASSERT_NOT_NULL(handle);

    TEST_ASSERT_EQUAL(ESP_OK, esp_timer_start_once(handle, kMediumUs));
    TEST_ASSERT_EQUAL(ESP_OK, esp_timer_stop(handle));
    TEST_ASSERT_FALSE(esp_timer_is_active(handle));

    vTaskDelay(pdMS_TO_TICKS(3 * kMediumUs / 1000));
    TEST_ASSERT_EQUAL(0, g_calls.load());
    stop_and_delete(handle);
}

TEST_CASE("esp_timer: stop on a stopped timer is rejected", "[esp_timer]") {
    esp_timer_handle_t handle = create_timer();
    TEST_ASSERT_NOT_NULL(handle);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, esp_timer_stop(handle));
    stop_and_delete(handle);
}

TEST_CASE("esp_timer: stop_blocking waits for a running callback",
          "[esp_timer]") {
    reset_calls();
    esp_timer_handle_t handle = create_timer(&slow_cb);
    TEST_ASSERT_NOT_NULL(handle);

    TEST_ASSERT_EQUAL(ESP_OK, esp_timer_start_once(handle, kShortUs));
    for (int waited = 0; !g_callback_started.load() && waited < kTimeoutMs;
         waited += 10) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    TEST_ASSERT_TRUE(g_callback_started.load());
    TEST_ASSERT_TRUE(esp_timer_is_active(handle));

    TEST_ASSERT_EQUAL(ESP_OK, esp_timer_stop_blocking(handle, kForever));
    // The callback has finished by the time stop_blocking returns.
    TEST_ASSERT_TRUE(g_callback_finished.load());
    TEST_ASSERT_FALSE(esp_timer_is_active(handle));
    stop_and_delete(handle);
}

TEST_CASE("esp_timer: stop_blocking can time out", "[esp_timer]") {
    reset_calls();
    esp_timer_handle_t handle = create_timer(&slow_cb);
    TEST_ASSERT_NOT_NULL(handle);

    TEST_ASSERT_EQUAL(ESP_OK, esp_timer_start_once(handle, kShortUs));
    for (int waited = 0; !g_callback_started.load() && waited < kTimeoutMs;
         waited += 10) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    TEST_ASSERT_TRUE(g_callback_started.load());

    // The callback sleeps for 100 ms; one tick is not enough to finish.
    TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT, esp_timer_stop_blocking(handle, 1));
    TEST_ASSERT_EQUAL(ESP_OK, esp_timer_stop_blocking(handle, kForever));
    stop_and_delete(handle);
}

TEST_CASE("esp_timer: a callback can stop its own timer", "[esp_timer]") {
    reset_calls();
    esp_timer_handle_t handle = create_timer(&self_stop_cb);
    TEST_ASSERT_NOT_NULL(handle);
    g_self_handle = handle;

    TEST_ASSERT_EQUAL(ESP_OK, esp_timer_start_once(handle, kShortUs));
    TEST_ASSERT_TRUE(wait_for_calls(1));
    TEST_ASSERT_EQUAL(ESP_OK, g_stop_result.load());
    TEST_ASSERT_FALSE(esp_timer_is_active(handle));
    stop_and_delete(handle);
}

TEST_CASE("esp_timer: a periodic callback can stop its own timer",
          "[esp_timer]") {
    reset_calls();
    esp_timer_handle_t handle = create_timer(&self_stop_cb);
    TEST_ASSERT_NOT_NULL(handle);
    g_self_handle = handle;

    TEST_ASSERT_EQUAL(ESP_OK, esp_timer_start_periodic(handle, kShortUs));
    TEST_ASSERT_TRUE(wait_for_calls(1));
    TEST_ASSERT_EQUAL(ESP_OK, g_stop_result.load());
    vTaskDelay(pdMS_TO_TICKS(3 * kShortUs / 1000));
    TEST_ASSERT_EQUAL(1, g_calls.load());
    stop_and_delete(handle);
}

TEST_CASE("esp_timer: stop_blocking prevents a periodic re-arm",
          "[esp_timer]") {
    reset_calls();
    esp_timer_handle_t handle = create_timer(&slow_cb);
    TEST_ASSERT_NOT_NULL(handle);

    TEST_ASSERT_EQUAL(ESP_OK, esp_timer_start_periodic(handle, kShortUs));
    for (int waited = 0; !g_callback_started.load() && waited < kTimeoutMs;
         waited += 10) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    TEST_ASSERT_TRUE(g_callback_started.load());

    TEST_ASSERT_EQUAL(ESP_OK, esp_timer_stop_blocking(handle, kForever));
    const int after_stop = g_calls.load();
    vTaskDelay(pdMS_TO_TICKS(3 * kShortUs / 1000));
    TEST_ASSERT_EQUAL(after_stop, g_calls.load());
    stop_and_delete(handle);
}

// --- Queries ----------------------------------------------------------------

TEST_CASE("esp_timer: get_time advances monotonically", "[esp_timer]") {
    const int64_t first = esp_timer_get_time();
    vTaskDelay(pdMS_TO_TICKS(20));
    const int64_t second = esp_timer_get_time();
    TEST_ASSERT_GREATER_OR_EQUAL_INT64(first, second);
    TEST_ASSERT_GREATER_THAN_INT64(first, second);
}

TEST_CASE("esp_timer: get_next_alarm reports the earliest alarm",
          "[esp_timer]") {
    reset_calls();
    esp_timer_handle_t handle = create_timer();
    TEST_ASSERT_NOT_NULL(handle);

    const int64_t before = esp_timer_get_time();
    TEST_ASSERT_EQUAL(ESP_OK, esp_timer_start_once(handle, kMediumUs));
    const int64_t next = esp_timer_get_next_alarm();

    // The alarm is in the future and no later than the requested delay, plus
    // slack for the read itself and the tick rounding on the host.
    TEST_ASSERT_GREATER_THAN_INT64(before, next);
    TEST_ASSERT_LESS_OR_EQUAL_INT64(
        before + static_cast<int64_t>(kMediumUs) + 100'000, next);
    stop_and_delete(handle);
}

TEST_CASE("esp_timer: get_next_alarm_for_wake_up skips opted-out timers",
          "[esp_timer]") {
    reset_calls();
    esp_timer_handle_t skip = create_timer(&count_cb, &g_calls, true);
    esp_timer_handle_t wake = create_timer();
    TEST_ASSERT_NOT_NULL(skip);
    TEST_ASSERT_NOT_NULL(wake);

    const int64_t now = esp_timer_get_time();
    TEST_ASSERT_EQUAL(ESP_OK, esp_timer_start_once(skip, kShortUs));
    TEST_ASSERT_EQUAL(ESP_OK, esp_timer_start_once_at(
                                  wake, static_cast<uint64_t>(now) + kLongUs));

    const int64_t wake_alarm = esp_timer_get_next_alarm_for_wake_up();
    // The skipped 50 ms timer must not be reported; the 500 ms timer is.
    TEST_ASSERT_GREATER_THAN_INT64(
        now + static_cast<int64_t>(kShortUs) + 10'000, wake_alarm);
    stop_and_delete(skip);
    stop_and_delete(wake);
}

TEST_CASE("esp_timer: get_period reports 0 for one-shot timers",
          "[esp_timer]") {
    esp_timer_handle_t handle = create_timer();
    TEST_ASSERT_NOT_NULL(handle);
    uint64_t period = 123;
    TEST_ASSERT_EQUAL(ESP_OK, esp_timer_get_period(handle, &period));
    TEST_ASSERT_EQUAL_UINT64(0, period);

    TEST_ASSERT_EQUAL(ESP_OK, esp_timer_start_periodic(handle, kShortUs));
    TEST_ASSERT_EQUAL(ESP_OK, esp_timer_get_period(handle, &period));
    TEST_ASSERT_EQUAL_UINT64(kShortUs, period);
    stop_and_delete(handle);
}

TEST_CASE("esp_timer: get_period rejects null arguments", "[esp_timer]") {
    esp_timer_handle_t handle = create_timer();
    TEST_ASSERT_NOT_NULL(handle);
    uint64_t period = 0;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,
                      esp_timer_get_period(nullptr, &period));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,
                      esp_timer_get_period(handle, nullptr));
    stop_and_delete(handle);
}

TEST_CASE("esp_timer: get_expiry_time reports a one-shot alarm",
          "[esp_timer]") {
    esp_timer_handle_t handle = create_timer();
    TEST_ASSERT_NOT_NULL(handle);
    const int64_t before = esp_timer_get_time();
    TEST_ASSERT_EQUAL(ESP_OK, esp_timer_start_once(handle, kShortUs));

    uint64_t expiry = 0;
    TEST_ASSERT_EQUAL(ESP_OK, esp_timer_get_expiry_time(handle, &expiry));
    TEST_ASSERT_GREATER_OR_EQUAL_UINT64(
        static_cast<uint64_t>(before) + kShortUs, expiry);
    TEST_ASSERT_LESS_OR_EQUAL_UINT64(
        static_cast<uint64_t>(before) + kShortUs + 100'000, expiry);
    stop_and_delete(handle);
}

TEST_CASE("esp_timer: get_expiry_time rejects a periodic timer",
          "[esp_timer]") {
    esp_timer_handle_t handle = create_timer();
    TEST_ASSERT_NOT_NULL(handle);
    TEST_ASSERT_EQUAL(ESP_OK, esp_timer_start_periodic(handle, kShortUs));

    uint64_t expiry = 0;
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_SUPPORTED,
                      esp_timer_get_expiry_time(handle, &expiry));
    stop_and_delete(handle);
}

TEST_CASE("esp_timer: is_active follows the armed and callback states",
          "[esp_timer]") {
    reset_calls();
    esp_timer_handle_t handle = create_timer();
    TEST_ASSERT_NOT_NULL(handle);
    TEST_ASSERT_FALSE(esp_timer_is_active(handle));

    TEST_ASSERT_EQUAL(ESP_OK, esp_timer_start_once(handle, kShortUs));
    TEST_ASSERT_TRUE(esp_timer_is_active(handle));
    TEST_ASSERT_TRUE(wait_for_calls(1));
    TEST_ASSERT_TRUE(wait_for_inactive(handle));
    TEST_ASSERT_FALSE(esp_timer_is_active(nullptr));
    stop_and_delete(handle);
}

TEST_CASE("esp_timer: dump writes the active timers", "[esp_timer]") {
    esp_timer_handle_t handle = create_timer();
    TEST_ASSERT_NOT_NULL(handle);
    TEST_ASSERT_EQUAL(ESP_OK, esp_timer_start_once(handle, kLongUs));

    FILE* stream = tmpfile();
    TEST_ASSERT_NOT_NULL(stream);
    TEST_ASSERT_EQUAL(ESP_OK, esp_timer_dump(stream));

    TEST_ASSERT_EQUAL(0, std::fflush(stream));
    TEST_ASSERT_EQUAL(0, std::fseek(stream, 0, SEEK_END));
    const long size = std::ftell(stream);
    TEST_ASSERT_GREATER_THAN(0, size);
    std::rewind(stream);

    char buffer[256] = {};
    const std::size_t read = std::fread(buffer, 1, sizeof(buffer) - 1, stream);
    TEST_ASSERT_GREATER_THAN(0, read);
    TEST_ASSERT_NOT_NULL(std::strstr(buffer, "Name"));

    std::fclose(stream);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, esp_timer_dump(nullptr));
    stop_and_delete(handle);
}

// --- Deleting ---------------------------------------------------------------

TEST_CASE("esp_timer: delete refuses an armed timer", "[esp_timer]") {
    reset_calls();
    esp_timer_handle_t handle = create_timer();
    TEST_ASSERT_NOT_NULL(handle);
    TEST_ASSERT_EQUAL(ESP_OK, esp_timer_start_once(handle, kLongUs));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, esp_timer_delete(handle));
    stop_and_delete(handle);
}

TEST_CASE("esp_timer: delete accepts a stopped timer", "[esp_timer]") {
    esp_timer_handle_t handle = create_timer();
    TEST_ASSERT_NOT_NULL(handle);
    TEST_ASSERT_EQUAL(ESP_OK, esp_timer_start_once(handle, kLongUs));
    TEST_ASSERT_EQUAL(ESP_OK, esp_timer_stop(handle));
    TEST_ASSERT_EQUAL(ESP_OK, esp_timer_delete(handle));
}

// --- Target-specific surface ------------------------------------------------

#if defined(CONFIG_IDF_TARGET_LINUX)
TEST_CASE("esp_timer: ETM alarm event is unsupported on the host",
          "[esp_timer]") {
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,
                      esp_timer_new_etm_alarm_event(nullptr));

    esp_etm_event_handle_t event = nullptr;
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_SUPPORTED,
                      esp_timer_new_etm_alarm_event(&event));
}
#elif defined(CONFIG_SOC_SYSTIMER_SUPPORT_ETM) && \
    CONFIG_SOC_SYSTIMER_SUPPORT_ETM
TEST_CASE("esp_timer: ETM alarm event is created or unsupported",
          "[esp_timer]") {
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,
                      esp_timer_new_etm_alarm_event(nullptr));

    esp_etm_event_handle_t event = nullptr;
    const esp_err_t err = esp_timer_new_etm_alarm_event(&event);
    TEST_ASSERT_TRUE(err == ESP_OK || err == ESP_ERR_NOT_SUPPORTED);
    if (err == ESP_OK) {
        TEST_ASSERT_NOT_NULL(event);
        TEST_ASSERT_EQUAL(ESP_OK, esp_etm_del_event(event));
    }
}
#endif

// --- De-initialization (last: it changes global state) ----------------------

TEST_CASE("esp_timer: deinit refuses while a timer is armed", "[esp_timer]") {
    reset_calls();
    esp_timer_handle_t handle = create_timer();
    TEST_ASSERT_NOT_NULL(handle);
    TEST_ASSERT_EQUAL(ESP_OK, esp_timer_start_once(handle, kLongUs));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, esp_timer_deinit());
    stop_and_delete(handle);
}

TEST_CASE("esp_timer: deinit and re-init leave a usable library",
          "[esp_timer]") {
    const esp_err_t err = esp_timer_deinit();
    if (err == ESP_OK) {
        TEST_ASSERT_EQUAL(ESP_OK, esp_timer_init());
    } else {
        // The chip may have system timers that keep the library alive.
        TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, err);
    }

    esp_timer_handle_t handle = create_timer();
    TEST_ASSERT_NOT_NULL(handle);
    TEST_ASSERT_EQUAL(ESP_OK, esp_timer_delete(handle));
}
