// Tests for `espidf_cpp::RingBuffer<ElementType>`.
//
// The wrapper is a typed, no-split FreeRTOS ring buffer: it stores only
// `ElementType`, keeps a configurable number of items, and receives into a
// `std::optional`. These tests cover that contract on the ESP-IDF `linux`
// target's POSIX FreeRTOS implementation:
//
//   * creation (handle is valid) and an initially empty buffer;
//   * FIFO ordering of a sent sequence;
//   * fullness — send fails when the configured item count is reached, and
//     succeeds again after a receive frees a slot;
//   * the tick and chrono timeout overloads;
//   * `sendFromIsr()` / `receiveFromIsr()` in their task-context form;
//   * move construction / move assignment (exactly one owner of the handle);
//   * the compile-time contract (trivially copyable element, non-copyable
//     buffer).
//
// ESP-IDF's linux FreeRTOS port runs on POSIX threads, so blocking calls and
// ISR-style calls are exercised in their task-context form here.
// NOLINTBEGIN
#pragma GCC diagnostic push  // NOLINT(ignored diagnostics)
#include "freertos/FreeRTOS.h"
#include "freertos/ringbuf.h"
#pragma GCC diagnostic pop
// NOLINTEND

#include <array>
#include <chrono>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>

#include <cstdint>

#include "espidf_cpp/ring_buffer.hpp"
#include "unity.h"

namespace {

using thermo::espidf_cpp::RingBuffer;

// A small, trivially copyable message.
struct Reading {
    std::int16_t temp_c_x100;
    std::uint8_t sensor;

    friend bool operator==(Reading const&, Reading const&) = default;
};

// Receives with no wait: an item, or `std::nullopt` if the buffer is empty.
std::optional<Reading> receive_now(RingBuffer<Reading>& buffer) {
    return buffer.receive(static_cast<TickType_t>(0));
}

}  // namespace

// --- Creation / destruction -------------------------------------------------

TEST_CASE("ring_buffer: creation yields a valid handle", "[ring_buffer]") {
    const RingBuffer<Reading> buffer{4};
    TEST_ASSERT_NOT_NULL(buffer.handle());
}

TEST_CASE("ring_buffer: a new buffer is empty", "[ring_buffer]") {
    RingBuffer<Reading> buffer{4};
    TEST_ASSERT_FALSE(receive_now(buffer).has_value());
}

// --- Send / receive round-trips --------------------------------------------

TEST_CASE("ring_buffer: a sent item is received unchanged", "[ring_buffer]") {
    RingBuffer<Reading> buffer{2};
    const Reading sent{2350, 2};

    TEST_ASSERT_TRUE(buffer.send(sent, static_cast<TickType_t>(0)));

    const std::optional<Reading> received = receive_now(buffer);
    TEST_ASSERT_TRUE(received.has_value());
    TEST_ASSERT_TRUE(sent == *received);
}

TEST_CASE("ring_buffer: items are received in the order they were sent",
          "[ring_buffer]") {
    RingBuffer<Reading> buffer{3};

    TEST_ASSERT_TRUE(buffer.send(Reading{10, 0}, static_cast<TickType_t>(0)));
    TEST_ASSERT_TRUE(buffer.send(Reading{20, 1}, static_cast<TickType_t>(0)));
    TEST_ASSERT_TRUE(buffer.send(Reading{30, 2}, static_cast<TickType_t>(0)));

    constexpr std::array<std::int16_t, 3> expected_values{10, 20, 30};
    for (const std::int16_t expected : expected_values) {
        const std::optional<Reading> received = receive_now(buffer);
        TEST_ASSERT_TRUE(received.has_value());
        TEST_ASSERT_EQUAL_INT16(expected, received->temp_c_x100);
    }
    TEST_ASSERT_FALSE(receive_now(buffer).has_value());
}

TEST_CASE("ring_buffer: a receive frees a slot for the next send",
          "[ring_buffer]") {
    RingBuffer<Reading> buffer{2};

    TEST_ASSERT_TRUE(buffer.send(Reading{1, 0}, static_cast<TickType_t>(0)));
    TEST_ASSERT_TRUE(receive_now(buffer).has_value());
    TEST_ASSERT_TRUE(buffer.send(Reading{2, 0}, static_cast<TickType_t>(0)));

    const std::optional<Reading> received = receive_now(buffer);
    TEST_ASSERT_TRUE(received.has_value());
    TEST_ASSERT_EQUAL_INT16(2, received->temp_c_x100);
}

// --- Fullness ---------------------------------------------------------------

TEST_CASE("ring_buffer: send fails when every configured slot is taken",
          "[ring_buffer]") {
    RingBuffer<Reading> buffer{2};

    TEST_ASSERT_TRUE(buffer.send(Reading{1, 0}, static_cast<TickType_t>(0)));
    TEST_ASSERT_TRUE(buffer.send(Reading{2, 0}, static_cast<TickType_t>(0)));
    // No room and no wait: the third send is refused, not silently dropped.
    TEST_ASSERT_FALSE(buffer.send(Reading{3, 0}, static_cast<TickType_t>(0)));

    // The two stored items are untouched.
    const std::optional<Reading> first = receive_now(buffer);
    const std::optional<Reading> second = receive_now(buffer);
    TEST_ASSERT_TRUE(first.has_value());
    TEST_ASSERT_TRUE(second.has_value());
    TEST_ASSERT_EQUAL_INT16(1, first->temp_c_x100);
    TEST_ASSERT_EQUAL_INT16(2, second->temp_c_x100);
    TEST_ASSERT_FALSE(receive_now(buffer).has_value());
}

TEST_CASE("ring_buffer: the chrono-send overload reports a full buffer",
          "[ring_buffer]") {
    RingBuffer<Reading> buffer{2};

    TEST_ASSERT_TRUE(buffer.send(Reading{1, 0}, std::chrono::milliseconds{0}));
    TEST_ASSERT_TRUE(buffer.send(Reading{2, 0}, std::chrono::milliseconds{0}));
    TEST_ASSERT_FALSE(buffer.send(Reading{3, 0}, std::chrono::milliseconds{0}));
}

// --- Timeouts ---------------------------------------------------------------

TEST_CASE("ring_buffer: a long receive times out on an empty buffer",
          "[ring_buffer]") {
    RingBuffer<Reading> buffer{2};

    // A short, real wait (20 ms) with nothing to receive. This also checks
    // that the tick and chrono overloads agree on "no item".
    TEST_ASSERT_FALSE(
        buffer.receive(std::chrono::milliseconds{20}).has_value());
    TEST_ASSERT_FALSE(buffer.receive(pdMS_TO_TICKS(20)).has_value());
}

TEST_CASE("ring_buffer: the chrono-receive overload works", "[ring_buffer]") {
    RingBuffer<Reading> buffer{2};
    TEST_ASSERT_TRUE(buffer.send(Reading{42, 3}, std::chrono::milliseconds{0}));

    const std::optional<Reading> received =
        buffer.receive(std::chrono::milliseconds{0});
    TEST_ASSERT_TRUE(received.has_value());
    TEST_ASSERT_EQUAL_INT16(42, received->temp_c_x100);
    TEST_ASSERT_EQUAL_UINT8(3, received->sensor);
}

// --- ISR-safe entry points --------------------------------------------------

TEST_CASE("ring_buffer: sendFromIsr delivers like send", "[ring_buffer]") {
    RingBuffer<Reading> buffer{2};

    TEST_ASSERT_TRUE(buffer.sendFromIsr(Reading{700, 2}));

    const std::optional<Reading> received = receive_now(buffer);
    TEST_ASSERT_TRUE(received.has_value());
    TEST_ASSERT_TRUE((Reading{700, 2} == *received));
}

TEST_CASE("ring_buffer: sendFromIsr reports a full buffer", "[ring_buffer]") {
    RingBuffer<Reading> buffer{2};

    TEST_ASSERT_TRUE(buffer.sendFromIsr(Reading{1, 0}));
    TEST_ASSERT_TRUE(buffer.sendFromIsr(Reading{2, 0}));
    TEST_ASSERT_FALSE(buffer.sendFromIsr(Reading{3, 0}));
}

TEST_CASE("ring_buffer: receiveFromIsr retrieves an item", "[ring_buffer]") {
    RingBuffer<Reading> buffer{2};
    TEST_ASSERT_TRUE(buffer.send(Reading{55, 1}, static_cast<TickType_t>(0)));

    const std::optional<Reading> received = buffer.receiveFromIsr();
    TEST_ASSERT_TRUE(received.has_value());
    TEST_ASSERT_TRUE((Reading{55, 1} == *received));
    TEST_ASSERT_FALSE(buffer.receiveFromIsr().has_value());
}

// --- Move semantics ---------------------------------------------------------

TEST_CASE("ring_buffer: move construction transfers the handle",
          "[ring_buffer]") {
    RingBuffer<Reading> source{2};
    RingbufHandle_t const original = source.handle();
    TEST_ASSERT_TRUE(source.send(Reading{9, 1}, static_cast<TickType_t>(0)));

    RingBuffer<Reading> moved{std::move(source)};
    TEST_ASSERT_EQUAL_PTR(original, moved.handle());

    const std::optional<Reading> received = receive_now(moved);
    TEST_ASSERT_TRUE(received.has_value());
    TEST_ASSERT_EQUAL_INT16(9, received->temp_c_x100);
}

TEST_CASE("ring_buffer: move assignment transfers the handle",
          "[ring_buffer]") {
    RingBuffer<Reading> source{2};
    RingbufHandle_t const original = source.handle();
    TEST_ASSERT_TRUE(source.send(Reading{4, 0}, static_cast<TickType_t>(0)));
    RingBuffer<Reading> target{2};

    target = std::move(source);

    TEST_ASSERT_EQUAL_PTR(original, target.handle());
    const std::optional<Reading> received = receive_now(target);
    TEST_ASSERT_TRUE(received.has_value());
    TEST_ASSERT_EQUAL_INT16(4, received->temp_c_x100);
}

// --- Compile-time contract --------------------------------------------------

TEST_CASE("ring_buffer: copy is disabled, move is enabled", "[ring_buffer]") {
    static_assert(!std::is_copy_constructible_v<RingBuffer<Reading>>);
    static_assert(!std::is_copy_assignable_v<RingBuffer<Reading>>);
    static_assert(std::is_move_constructible_v<RingBuffer<Reading>>);
    static_assert(std::is_move_assignable_v<RingBuffer<Reading>>);
    static_assert(std::is_trivially_copyable_v<Reading>);
    TEST_ASSERT_TRUE(true);
}
