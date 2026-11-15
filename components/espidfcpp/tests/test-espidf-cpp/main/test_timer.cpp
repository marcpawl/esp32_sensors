// Tests for `espidf_cpp::Timer`.
//
// The wrapper is an RAII owner of one `esp_timer`. These tests use the
// recording fake in `fake_esp_timer.cpp` and cover construction, each
// delegating member function, the queries, move-only ownership, and
// destruction. The fake does not fire callbacks: the wrapper only owns and arms
// the timer, so delegation is what there is to test here.

#include <chrono>
#include <type_traits>
#include <utility>

#include <cstdint>

#include "espidf_cpp/timer.hpp"
#include "fake_esp_timer.hpp"
#include "unity.h"

namespace {

using thermo::espidf_cpp::Timer;

int g_callback_arg = 0;

void timer_callback(void* /*arg*/) {}

}  // namespace

TEST_CASE("espidf_cpp timer: construction creates the timer with its arguments",
          "[espidf_cpp_timer]") {
    fake_esp_timer::reset();
    {
        Timer timer(&timer_callback, &g_callback_arg, "sensor", true);
        TEST_ASSERT_EQUAL_INT(1, fake_esp_timer::state().create_calls);
        TEST_ASSERT_TRUE(fake_esp_timer::state().callback == &timer_callback);
        TEST_ASSERT_EQUAL_PTR(&g_callback_arg, fake_esp_timer::state().arg);
        TEST_ASSERT_EQUAL_STRING("sensor",
                                 fake_esp_timer::state().name.c_str());
        TEST_ASSERT_TRUE(fake_esp_timer::state().skip_unhandled_events);
        TEST_ASSERT_NOT_NULL(timer.handle());
    }
    TEST_ASSERT_EQUAL_INT(1, fake_esp_timer::state().delete_calls);
}

TEST_CASE("espidf_cpp timer: construction defaults the name to timer",
          "[espidf_cpp_timer]") {
    fake_esp_timer::reset();
    Timer timer(&timer_callback, nullptr);
    TEST_ASSERT_EQUAL_STRING("timer", fake_esp_timer::state().name.c_str());
    TEST_ASSERT_FALSE(fake_esp_timer::state().skip_unhandled_events);
}

TEST_CASE("espidf_cpp timer: destruction stops and deletes the timer",
          "[espidf_cpp_timer]") {
    fake_esp_timer::reset();
    {
        Timer timer(&timer_callback, nullptr);
        TEST_ASSERT_EQUAL_INT(0, fake_esp_timer::state().delete_calls);
    }
    TEST_ASSERT_EQUAL_INT(1, fake_esp_timer::state().stop_blocking_calls);
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX,
                             fake_esp_timer::state().last_timeout_ticks);
    TEST_ASSERT_EQUAL_INT(1, fake_esp_timer::state().delete_calls);
}

TEST_CASE("espidf_cpp timer: start_once delegates a one-shot start",
          "[espidf_cpp_timer]") {
    fake_esp_timer::reset();
    Timer timer(&timer_callback, nullptr);

    TEST_ASSERT_EQUAL(ESP_OK, timer.start_once(std::chrono::milliseconds{50}));
    TEST_ASSERT_EQUAL_INT(1, fake_esp_timer::state().start_once_calls);
    TEST_ASSERT_EQUAL_UINT64(50'000, fake_esp_timer::state().last_delay_us);
    TEST_ASSERT_TRUE(timer.is_active());
    TEST_ASSERT_FALSE(fake_esp_timer::state().periodic);
}

TEST_CASE("espidf_cpp timer: start_periodic delegates a periodic start",
          "[espidf_cpp_timer]") {
    fake_esp_timer::reset();
    Timer timer(&timer_callback, nullptr);

    TEST_ASSERT_EQUAL(ESP_OK,
                      timer.start_periodic(std::chrono::milliseconds{100}));
    TEST_ASSERT_EQUAL_INT(1, fake_esp_timer::state().start_periodic_calls);
    TEST_ASSERT_EQUAL_UINT64(100'000, fake_esp_timer::state().last_delay_us);
    TEST_ASSERT_TRUE(fake_esp_timer::state().periodic);
}

TEST_CASE("espidf_cpp timer: restart delegates a new delay",
          "[espidf_cpp_timer]") {
    fake_esp_timer::reset();
    Timer timer(&timer_callback, nullptr);
    TEST_ASSERT_EQUAL(ESP_OK, timer.start_once(std::chrono::milliseconds{100}));

    TEST_ASSERT_EQUAL(ESP_OK, timer.restart(std::chrono::milliseconds{25}));
    TEST_ASSERT_EQUAL_INT(1, fake_esp_timer::state().restart_calls);
    TEST_ASSERT_EQUAL_UINT64(25'000, fake_esp_timer::state().last_delay_us);
}

TEST_CASE("espidf_cpp timer: stop and cancel delegate", "[espidf_cpp_timer]") {
    fake_esp_timer::reset();
    Timer timer(&timer_callback, nullptr);
    TEST_ASSERT_EQUAL(ESP_OK, timer.start_once(std::chrono::milliseconds{100}));

    TEST_ASSERT_EQUAL(ESP_OK, timer.stop());
    TEST_ASSERT_EQUAL_INT(1, fake_esp_timer::state().stop_calls);
    TEST_ASSERT_FALSE(timer.is_active());

    TEST_ASSERT_EQUAL(ESP_OK, timer.start_once(std::chrono::milliseconds{100}));
    timer.cancel();
    TEST_ASSERT_EQUAL_INT(2, fake_esp_timer::state().stop_calls);
    TEST_ASSERT_FALSE(timer.is_active());
}

TEST_CASE("espidf_cpp timer: stop_blocking delegates its timeout",
          "[espidf_cpp_timer]") {
    fake_esp_timer::reset();
    Timer timer(&timer_callback, nullptr);
    TEST_ASSERT_EQUAL(ESP_OK, timer.start_once(std::chrono::milliseconds{100}));

    TEST_ASSERT_EQUAL(ESP_OK, timer.stop_blocking(42));
    TEST_ASSERT_EQUAL_INT(1, fake_esp_timer::state().stop_blocking_calls);
    TEST_ASSERT_EQUAL_UINT32(42, fake_esp_timer::state().last_timeout_ticks);
    TEST_ASSERT_FALSE(timer.is_active());
}

TEST_CASE("espidf_cpp timer: period reports the periodic period",
          "[espidf_cpp_timer]") {
    fake_esp_timer::reset();
    Timer timer(&timer_callback, nullptr);
    TEST_ASSERT_EQUAL(ESP_OK,
                      timer.start_periodic(std::chrono::milliseconds{100}));

    TEST_ASSERT_EQUAL_INT64(100'000, timer.period().count());
}

TEST_CASE("espidf_cpp timer: period is zero for a one-shot timer",
          "[espidf_cpp_timer]") {
    fake_esp_timer::reset();
    Timer timer(&timer_callback, nullptr);
    TEST_ASSERT_EQUAL(ESP_OK, timer.start_once(std::chrono::milliseconds{100}));

    TEST_ASSERT_EQUAL_INT64(0, timer.period().count());
}

TEST_CASE("espidf_cpp timer: expiry reports a one-shot expiry",
          "[espidf_cpp_timer]") {
    fake_esp_timer::reset();
    Timer timer(&timer_callback, nullptr);
    TEST_ASSERT_EQUAL(ESP_OK, timer.start_once(std::chrono::milliseconds{100}));

    TEST_ASSERT_EQUAL_INT64(100'000, timer.expiry().count());
}

TEST_CASE("espidf_cpp timer: move construction transfers the handle",
          "[espidf_cpp_timer]") {
    fake_esp_timer::reset();
    Timer source(&timer_callback, nullptr);
    esp_timer_handle_t const handle = source.handle();

    Timer moved(std::move(source));
    // Inspecting the moved-from object is deliberate: the wrapper promises it
    // no longer owns a timer.
    // NOLINTNEXTLINE(clang-analyzer-cplusplus.Move)
    TEST_ASSERT_NULL(source.handle());
    TEST_ASSERT_EQUAL_PTR(handle, moved.handle());
    TEST_ASSERT_EQUAL_INT(0, fake_esp_timer::state().delete_calls);
}

TEST_CASE("espidf_cpp timer: move assignment releases the old timer",
          "[espidf_cpp_timer]") {
    fake_esp_timer::reset();
    Timer target(&timer_callback, nullptr);
    Timer source(&timer_callback, nullptr);
    esp_timer_handle_t const source_handle = source.handle();

    target = std::move(source);
    // Inspecting the moved-from object is deliberate: the wrapper promises it
    // no longer owns a timer.
    // NOLINTNEXTLINE(clang-analyzer-cplusplus.Move)
    TEST_ASSERT_NULL(source.handle());
    TEST_ASSERT_EQUAL_PTR(source_handle, target.handle());
    TEST_ASSERT_EQUAL_INT(1, fake_esp_timer::state().delete_calls);
}

TEST_CASE("espidf_cpp timer: the class is move-only", "[espidf_cpp_timer]") {
    static_assert(!std::is_copy_constructible_v<Timer>);
    static_assert(!std::is_copy_assignable_v<Timer>);
    static_assert(std::is_nothrow_move_constructible_v<Timer>);
    static_assert(std::is_nothrow_move_assignable_v<Timer>);
    TEST_PASS();
}
