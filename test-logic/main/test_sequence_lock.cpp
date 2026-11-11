// Tests for `SequenceLock<Model>` (`logic/sequence_lock.hpp`).
//
// The lock stores a versioned model: `read()` returns a `VersionedModel`
// (model plus the sequence number it was read at) and retries while a writer
// has the sequence odd, so a reader never accepts a torn snapshot. `write()`
// takes that versioned model back and accepts it only when its version still
// matches the stored sequence, so a stale writer cannot overwrite a newer
// model.
//
// The single-threaded tests pin the round trips, the version progression, and
// the rejection of a stale or invented version. The concurrent tests drive real
// FreeRTOS tasks: two writers doing read-modify-write increments must not lose
// an update, and readers must never observe a torn model while writers replace
// it.
// NOLINTBEGIN
#pragma GCC diagnostic push  // NOLINT(ignored diagnostics)
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#pragma GCC diagnostic pop

// NOLINTEND

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "logic/sequence_lock.hpp"
#include "unity.h"

namespace {

using thermo::SequenceLock;

// A model with mixed field sizes and trailing padding, so a snapshot copy is
// exercised across more than one aligned word.
struct Reading {
    std::uint32_t id = 0;
    std::int32_t temperature_c_x100 = 0;
    std::uint8_t flags[5] = {};
};

using ReadingLock = SequenceLock<Reading>;

constexpr ReadingLock::WriteResult kStored =
    ReadingLock::WriteResult::kStored;
constexpr ReadingLock::WriteResult kRejected =
    ReadingLock::WriteResult::kRejected;

void assert_readings_equal(Reading const& expected, Reading const& actual) {
    TEST_ASSERT_EQUAL_UINT32(expected.id, actual.id);
    TEST_ASSERT_EQUAL_INT32(expected.temperature_c_x100,
                            actual.temperature_c_x100);
    for (std::size_t i = 0; i < sizeof(expected.flags); ++i) {
        TEST_ASSERT_EQUAL_UINT8(expected.flags[i], actual.flags[i]);
    }
}

}  // namespace

// --- Single-threaded round trips --------------------------------------------

TEST_CASE("sequence_lock: a default-constructed lock reads version zero",
          "[sequence_lock]") {
    const SequenceLock<Reading> lock;
    const auto reading = lock.read();
    TEST_ASSERT_EQUAL_UINT32(0, reading.version);
    assert_readings_equal(Reading{}, reading.model);
}

TEST_CASE("sequence_lock: constructs with an initial model",
          "[sequence_lock]") {
    const Reading initial{5, 500, {5, 5, 5, 5, 5}};
    const SequenceLock<Reading> lock(initial);

    const auto reading = lock.read();
    TEST_ASSERT_EQUAL_UINT32(0, reading.version);
    assert_readings_equal(initial, reading.model);
}

TEST_CASE("sequence_lock: a fresh version publishes the model",
          "[sequence_lock]") {
    SequenceLock<Reading> lock;
    const Reading expected{7, 2150, {1, 2, 3, 4, 5}};

    auto snapshot = lock.read();
    snapshot.model = expected;
    TEST_ASSERT_TRUE(lock.write(snapshot) == kStored);

    const auto actual = lock.read();
    TEST_ASSERT_EQUAL_UINT32(snapshot.version + 2, actual.version);
    assert_readings_equal(expected, actual.model);
}

TEST_CASE("sequence_lock: each successful write advances the version by two",
          "[sequence_lock]") {
    SequenceLock<Reading> lock;

    auto snapshot = lock.read();
    TEST_ASSERT_EQUAL_UINT32(0, snapshot.version);
    snapshot.model.id = 1;
    TEST_ASSERT_TRUE(lock.write(snapshot) == kStored);

    snapshot = lock.read();
    TEST_ASSERT_EQUAL_UINT32(2, snapshot.version);
    snapshot.model.id = 2;
    TEST_ASSERT_TRUE(lock.write(snapshot) == kStored);

    snapshot = lock.read();
    TEST_ASSERT_EQUAL_UINT32(4, snapshot.version);
    TEST_ASSERT_EQUAL_UINT32(2, snapshot.model.id);
}

TEST_CASE("sequence_lock: a stale version cannot overwrite a newer model",
          "[sequence_lock]") {
    SequenceLock<Reading> lock;
    auto first = lock.read();
    auto second = lock.read();
    TEST_ASSERT_EQUAL_UINT32(first.version, second.version);

    first.model.id = 11;
    second.model.id = 22;
    TEST_ASSERT_TRUE(lock.write(first) == kStored);

    // `second` still carries the version it was read at, so it is stale.
    TEST_ASSERT_TRUE(lock.write(second) == kRejected);

    const auto actual = lock.read();
    TEST_ASSERT_EQUAL_UINT32(11, actual.model.id);
    TEST_ASSERT_EQUAL_UINT32(2, actual.version);
}

TEST_CASE("sequence_lock: a version that is not current is rejected",
          "[sequence_lock]") {
    SequenceLock<Reading> lock;
    const Reading model{9, 900, {9, 9, 9, 9, 9}};

    // An odd version can never be current (it marks a write in progress), and
    // a future version does not match the stored zero.
    TEST_ASSERT_TRUE(lock.write({1, model}) == kRejected);
    TEST_ASSERT_TRUE(lock.write({99, model}) == kRejected);

    const auto actual = lock.read();
    TEST_ASSERT_EQUAL_UINT32(0, actual.version);
    assert_readings_equal(Reading{}, actual.model);
}

TEST_CASE("sequence_lock: a re-read version can write again",
          "[sequence_lock]") {
    SequenceLock<Reading> lock;

    auto snapshot = lock.read();
    snapshot.model.id = 1;
    TEST_ASSERT_TRUE(lock.write(snapshot) == kStored);
    TEST_ASSERT_TRUE(lock.write(snapshot) == kRejected);

    // The caller that lost the race must read again before its next write.
    snapshot = lock.read();
    snapshot.model.id = 3;
    TEST_ASSERT_TRUE(lock.write(snapshot) == kStored);

    const auto actual = lock.read();
    TEST_ASSERT_EQUAL_UINT32(3, actual.model.id);
    TEST_ASSERT_EQUAL_UINT32(4, actual.version);
}

TEST_CASE("sequence_lock: read leaves the stored model in place",
          "[sequence_lock]") {
    SequenceLock<Reading> lock;
    const Reading expected{4, 400, {4, 4, 4, 4, 4}};

    auto snapshot = lock.read();
    snapshot.model = expected;
    TEST_ASSERT_TRUE(lock.write(snapshot) == kStored);

    for (int read = 0; read < 3; ++read) {
        const auto actual = lock.read();
        TEST_ASSERT_EQUAL_UINT32(2, actual.version);
        assert_readings_equal(expected, actual.model);
    }
}

TEST_CASE("sequence_lock: works with a scalar model", "[sequence_lock]") {
    using ScalarLock = SequenceLock<std::uint64_t>;
    ScalarLock lock;
    auto snapshot = lock.read();
    snapshot.model = UINT64_C(0x1234567890ABCDEF);
    TEST_ASSERT_TRUE(lock.write(snapshot) == ScalarLock::WriteResult::kStored);

    const auto value = lock.read();
    TEST_ASSERT_EQUAL_UINT64(UINT64_C(0x1234567890ABCDEF), value.model);
}

// --- Concurrent reads and writes --------------------------------------------

namespace {

struct Counter {
    std::uint32_t count = 0;
};

struct IncrementContext {
    SequenceLock<Counter>* lock = nullptr;
    std::atomic<std::uint32_t>* finished = nullptr;
    std::uint32_t iterations = 0;
};

// Read, increment, and write back. A failed write means another writer
// published first, so the snapshot must be re-read and the increment re-applied
// to the newer value.
void increment_task(void* arg) {
    auto& context = *static_cast<IncrementContext*>(arg);
    for (std::uint32_t i = 0; i < context.iterations; ++i) {
        auto snapshot = context.lock->read();
        snapshot.model.count += 1;
        while (context.lock->write(snapshot) ==
               SequenceLock<Counter>::WriteResult::kRejected) {
            snapshot = context.lock->read();
            snapshot.model.count += 1;
        }
        taskYIELD();
    }

    context.finished->fetch_add(1, std::memory_order_relaxed);
    vTaskDelete(nullptr);
}

// A model wide enough that a reader can plausibly catch a write in progress.
struct Block {
    static constexpr std::size_t kWords = 32;
    std::uint32_t words[kWords] = {};
};

struct BlockContext {
    SequenceLock<Block>* lock = nullptr;
    std::atomic<bool>* stop = nullptr;
    std::atomic<std::uint32_t>* finished = nullptr;
    std::uint32_t pattern = 0;
};

// Keep replacing the whole block with one repeated value. Every consistent
// snapshot must therefore have all words equal.
void block_writer_task(void* arg) {
    auto& context = *static_cast<BlockContext*>(arg);
    std::uint32_t value = context.pattern;

    while (!context.stop->load(std::memory_order_relaxed)) {
        auto snapshot = context.lock->read();
        for (std::uint32_t& word : snapshot.model.words) {
            word = value;
        }
        while (context.lock->write(snapshot) ==
               SequenceLock<Block>::WriteResult::kRejected) {
            snapshot = context.lock->read();
            for (std::uint32_t& word : snapshot.model.words) {
                word = value;
            }
        }
        ++value;
        taskYIELD();
    }

    context.finished->fetch_add(1, std::memory_order_relaxed);
    vTaskDelete(nullptr);
}

void create_task(TaskFunction_t entry, const char* name, void* arg,
                 TaskHandle_t* handle) {
    const UBaseType_t priority = uxTaskPriorityGet(nullptr);
    TEST_ASSERT_EQUAL(pdPASS,
                      xTaskCreate(entry, name, 4096, arg, priority, handle));
}

// Wait until every task has reported completion, failing after ten seconds.
void wait_for_tasks(std::atomic<std::uint32_t>& finished,
                    std::uint32_t expected) {
    const TickType_t start = xTaskGetTickCount();
    while (finished.load(std::memory_order_relaxed) < expected) {
        TEST_ASSERT_TRUE_MESSAGE((xTaskGetTickCount() - start) <
                                     pdMS_TO_TICKS(10000),
                                 "timed out waiting for a writer task");
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

}  // namespace

TEST_CASE("sequence_lock: concurrent writers do not lose an update",
          "[sequence_lock]") {
    SequenceLock<Counter> lock;
    std::atomic<std::uint32_t> finished{0};

    constexpr std::uint32_t kIterations = 500;
    IncrementContext first{&lock, &finished, kIterations};
    IncrementContext second{&lock, &finished, kIterations};

    TaskHandle_t first_handle = nullptr;
    TaskHandle_t second_handle = nullptr;
    create_task(increment_task, "seq-increment-a", &first, &first_handle);
    create_task(increment_task, "seq-increment-b", &second, &second_handle);

    wait_for_tasks(finished, 2);

    const auto final = lock.read();
    TEST_ASSERT_EQUAL_UINT32(2 * kIterations, final.model.count);
    // Every successful write advances the version by two, and retries do not.
    TEST_ASSERT_EQUAL_UINT32(4 * kIterations, final.version);
}

TEST_CASE("sequence_lock: concurrent readers never see a torn model",
          "[sequence_lock]") {
    SequenceLock<Block> lock;
    std::atomic<bool> stop{false};
    std::atomic<std::uint32_t> finished{0};

    BlockContext first{&lock, &stop, &finished, 0x11111111U};
    BlockContext second{&lock, &stop, &finished, 0x22222222U};

    TaskHandle_t first_handle = nullptr;
    TaskHandle_t second_handle = nullptr;
    create_task(block_writer_task, "seq-block-a", &first, &first_handle);
    create_task(block_writer_task, "seq-block-b", &second, &second_handle);

    std::uint32_t reads = 0;
    const TickType_t start = xTaskGetTickCount();
    while ((xTaskGetTickCount() - start) < pdMS_TO_TICKS(200)) {
        const auto block = lock.read();
        ++reads;

        const std::uint32_t expected = block.model.words[0];
        for (std::size_t i = 1; i < Block::kWords; ++i) {
            TEST_ASSERT_EQUAL_UINT32(expected, block.model.words[i]);
        }
        taskYIELD();
    }
    TEST_ASSERT_TRUE_MESSAGE(reads > 0, "no reader ever saw a value");

    stop.store(true, std::memory_order_relaxed);
    wait_for_tasks(finished, 2);

    // Once both writers are gone the cell must hold one complete model.
    const auto final = lock.read();
    const std::uint32_t pattern = final.model.words[0];
    for (std::size_t i = 1; i < Block::kWords; ++i) {
        TEST_ASSERT_EQUAL_UINT32(pattern, final.model.words[i]);
    }
}
