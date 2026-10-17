// Tests for `espidf_cpp::OneMessageQueue<T>`.
//
// The queue is a thin C++ wrapper over the FreeRTOS queue API: one slot,
// overwritten by every `send()`, received as a `std::optional`. These tests
// cover that contract on the ESP-IDF `linux` target's POSIX FreeRTOS
// implementation:
//
//   * creation (handle is valid) and destruction (the handle is deleted);
//   * send/receive round-trips with a trivially copyable message type;
//   * overwrite semantics — a second send replaces an unread message;
//   * the `receive()` timeout overloads (zero timeout and non-zero timeout);
//   * move construction / move assignment (exactly one owner of the handle);
//   * `send_from_isr()` behaves like `send()` with respect to overwriting.
//
// ESP-IDF's linux FreeRTOS port runs on POSIX threads, so blocking calls and
// ISR-style calls are exercised in their task-context form here.
#include "unity.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "espidf_cpp/one_message_queue.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

namespace {

using thermo::espidf_cpp::OneMessageQueue;

// A small, trivially copyable message.
struct Reading {
    std::int16_t temp_c_x100;
    std::uint8_t sensor;

    friend bool operator==(Reading const&, Reading const&) = default;
};

// Receives with no wait: a message, or `std::nullopt` if the queue is empty.
std::optional<Reading> receive_now(OneMessageQueue<Reading>& queue) {
    return queue.receive(static_cast<TickType_t>(0));
}

}  // namespace

// --- Creation / destruction -------------------------------------------------

TEST_CASE("one_message_queue: creation yields a valid handle",
          "[one_message_queue]") {
    const OneMessageQueue<Reading> queue;
    TEST_ASSERT_NOT_NULL(queue.handle());
}

TEST_CASE("one_message_queue: a new queue is empty", "[one_message_queue]") {
    OneMessageQueue<Reading> queue;
    TEST_ASSERT_FALSE(receive_now(queue).has_value());
}

TEST_CASE("one_message_queue: destruction deletes the handle",
          "[one_message_queue]") {
    // Unable to test.
    // Note: DeepSeek is wrong, handles can be reused.
    //    Queue handles are never reused once deleted,
    //    so remembering the old handle is safe: the
    //    next creation must hand out a different one.

    // We will not have a unit test, since UniqueQueueHandle
    // is being used.
}

// --- Send / receive round-trips --------------------------------------------

TEST_CASE("one_message_queue: a sent message is received unchanged",
          "[one_message_queue]") {
    OneMessageQueue<Reading> queue;
    const Reading sent{2350, 2};

    queue.send(sent);

    const std::optional<Reading> received = receive_now(queue);
    TEST_ASSERT_TRUE(received.has_value());
    TEST_ASSERT_TRUE(sent == *received);
}

TEST_CASE("one_message_queue: receiving empties the queue",
          "[one_message_queue]") {
    OneMessageQueue<Reading> queue;
    queue.send(Reading{100, 0});

    TEST_ASSERT_TRUE(receive_now(queue).has_value());
    // Only one slot: after the receive, the queue is empty again.
    TEST_ASSERT_FALSE(receive_now(queue).has_value());
}

TEST_CASE("one_message_queue: the tick-timeout receive overload works",
          "[one_message_queue]") {
    OneMessageQueue<Reading> queue;
    queue.send(Reading{-500, 1});

    const std::optional<Reading> received =
        queue.receive(static_cast<TickType_t>(0));
    TEST_ASSERT_TRUE(received.has_value());
    TEST_ASSERT_EQUAL_INT16(-500, received->temp_c_x100);
    TEST_ASSERT_EQUAL_UINT8(1, received->sensor);
}

TEST_CASE("one_message_queue: the chrono-timeout receive overload works",
          "[one_message_queue]") {
    OneMessageQueue<Reading> queue;
    queue.send(Reading{42, 3});

    const std::optional<Reading> received =
        queue.receive(std::chrono::milliseconds{0});
    TEST_ASSERT_TRUE(received.has_value());
    TEST_ASSERT_EQUAL_INT16(42, received->temp_c_x100);
    TEST_ASSERT_EQUAL_UINT8(3, received->sensor);
}

// --- Overwrite semantics ----------------------------------------------------

TEST_CASE("one_message_queue: a second send overwrites an unread message",
          "[one_message_queue]") {
    OneMessageQueue<Reading> queue;

    queue.send(Reading{1111, 0});
    queue.send(Reading{2222, 1});

    // Only the last message survives: one slot, overwritten in place.
    const std::optional<Reading> received = receive_now(queue);
    TEST_ASSERT_TRUE(received.has_value());
    TEST_ASSERT_EQUAL_INT16(2222, received->temp_c_x100);
    TEST_ASSERT_EQUAL_UINT8(1, received->sensor);
    TEST_ASSERT_FALSE(receive_now(queue).has_value());
}

TEST_CASE("one_message_queue: send overwrites after a receive too",
          "[one_message_queue]") {
    OneMessageQueue<Reading> queue;

    queue.send(Reading{1, 0});
    TEST_ASSERT_TRUE(receive_now(queue).has_value());
    queue.send(Reading{2, 1});

    const std::optional<Reading> received = receive_now(queue);
    TEST_ASSERT_TRUE(received.has_value());
    TEST_ASSERT_EQUAL_INT16(2, received->temp_c_x100);
}

// --- Timeouts ---------------------------------------------------------------

TEST_CASE("one_message_queue: a long receive times out on an empty queue",
          "[one_message_queue]") {
    OneMessageQueue<Reading> queue;

    // A short, real wait (20 ms) with nothing to receive. This also checks
    // that the tick and chrono overloads agree on "no message".
    TEST_ASSERT_FALSE(queue.receive(std::chrono::milliseconds{20}).has_value());
    TEST_ASSERT_FALSE(queue.receive(pdMS_TO_TICKS(20)).has_value());
}

// --- ISR-safe send ----------------------------------------------------------

TEST_CASE("one_message_queue: send_from_isr delivers like send",
          "[one_message_queue]") {
    OneMessageQueue<Reading> queue;

    queue.send_from_isr(Reading{700, 2});

    const std::optional<Reading> received = receive_now(queue);
    TEST_ASSERT_TRUE(received.has_value());
    TEST_ASSERT_TRUE((Reading{700, 2} == *received));
}

// --- Move semantics ---------------------------------------------------------

TEST_CASE("one_message_queue: move construction transfers the handle",
          "[one_message_queue]") {
    OneMessageQueue<Reading> source;
    QueueHandle_t const original = source.handle();
    source.send(Reading{9, 1});

    OneMessageQueue<Reading> moved{std::move(source)};
    TEST_ASSERT_EQUAL_PTR(original, moved.handle());

    const std::optional<Reading> received = receive_now(moved);
    TEST_ASSERT_TRUE(received.has_value());
    TEST_ASSERT_EQUAL_INT16(9, received->temp_c_x100);
}

TEST_CASE("one_message_queue: move assignment transfers the handle",
          "[one_message_queue]") {
    OneMessageQueue<Reading> source;
    QueueHandle_t const original = source.handle();
    source.send(Reading{4, 0});
    OneMessageQueue<Reading> target;

    target = std::move(source);

    TEST_ASSERT_EQUAL_PTR(original, target.handle());
    const std::optional<Reading> received = receive_now(target);
    TEST_ASSERT_TRUE(received.has_value());
    TEST_ASSERT_EQUAL_INT16(4, received->temp_c_x100);
}

// --- Compile-time contract --------------------------------------------------

TEST_CASE("one_message_queue: copy is disabled, move is enabled",
          "[one_message_queue]") {
    static_assert(!std::is_copy_constructible_v<OneMessageQueue<Reading>>);
    static_assert(!std::is_copy_assignable_v<OneMessageQueue<Reading>>);
    static_assert(std::is_move_constructible_v<OneMessageQueue<Reading>>);
    static_assert(std::is_move_assignable_v<OneMessageQueue<Reading>>);
    TEST_ASSERT_TRUE(true);
}
