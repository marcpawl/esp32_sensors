// Tests for `SequenceLock<Model, concurrent_writers>`
// (`utility/sequence_lock.hpp`).
//
// The lock stores a versioned model: `read()` returns a `VersionedModel`
// (model plus the sequence number it was read at) and retries while a writer
// has the sequence odd, so a reader never accepts a torn snapshot. The
// `ConcurrentWriters` parameter selects the writer contract:
//
//   - `Disable`: a single writer, `write()` returns void;
//   - `Enabled`: competing writers are rejected, and `write()` returns a
//     `WriteResult`.
//
// The single-threaded tests pin the round trips, the version progression, the
// rejection of a stale or invented version, and the two write forms. The
// concurrent tests drive real FreeRTOS tasks: two writers doing
// read-modify-write increments must not lose an update, and readers must never
// observe a torn model while writers replace it.
// NOLINTBEGIN
#pragma GCC diagnostic push  // NOLINT(ignored diagnostics)
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#pragma GCC diagnostic pop

// NOLINTEND

#include <atomic>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <string>
#include <type_traits>

#include "unity.h"
#include "utility/sequence_lock.hpp"

namespace {

using thermo::ConcurrentWriters;
using thermo::SequenceLock;

// A model with mixed field sizes and trailing padding, so a snapshot copy is
// exercised across more than one aligned word.
struct Reading {
    std::uint32_t id = 0;
    std::int32_t temperature_c_x100 = 0;
    std::uint8_t flags[5] = {};
};

using ReadingLock = SequenceLock<Reading, ConcurrentWriters::Enabled>;

constexpr ReadingLock::WriteResult kStored = ReadingLock::WriteResult::kStored;
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

template<typename Lock>
concept HasVoidWrite =
    requires(Lock& lock, typename Lock::VersionedModel const& model) {
        { lock.write(model) } -> std::same_as<void>;
    };

template<typename Lock>
concept HasResultWrite =
    requires(Lock& lock, typename Lock::VersionedModel const& model) {
        { lock.write(model) } -> std::same_as<typename Lock::WriteResult>;
    };

// A snapshot's version is const, so it cannot be re-pointed at a newer
// sequence; the caller must `read()` again instead.
static_assert(std::is_const_v<decltype(ReadingLock::VersionedModel::version)>);
static_assert(!std::is_assignable_v<
              decltype(ReadingLock::VersionedModel::version), std::uint32_t>);

}  // namespace

// --- Single-threaded round trips --------------------------------------------

TEST_CASE("sequence_lock: a default-constructed lock reads version zero",
          "[sequence_lock]") {
    const ReadingLock lock;
    const auto reading = lock.read();
    TEST_ASSERT_EQUAL_UINT32(0, reading.version);
    assert_readings_equal(Reading{}, reading.model);
}

TEST_CASE("sequence_lock: constructs with an initial model",
          "[sequence_lock]") {
    const Reading initial{5, 500, {5, 5, 5, 5, 5}};
    const ReadingLock lock(initial);

    const auto reading = lock.read();
    TEST_ASSERT_EQUAL_UINT32(0, reading.version);
    assert_readings_equal(initial, reading.model);
}

TEST_CASE("sequence_lock: a fresh version publishes the model",
          "[sequence_lock]") {
    ReadingLock lock;
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
    ReadingLock lock;

    auto first = lock.read();
    TEST_ASSERT_EQUAL_UINT32(0, first.version);
    first.model.id = 1;
    TEST_ASSERT_TRUE(lock.write(first) == kStored);

    auto second = lock.read();
    TEST_ASSERT_EQUAL_UINT32(2, second.version);
    second.model.id = 2;
    TEST_ASSERT_TRUE(lock.write(second) == kStored);

    const auto after = lock.read();
    TEST_ASSERT_EQUAL_UINT32(4, after.version);
    TEST_ASSERT_EQUAL_UINT32(2, after.model.id);
}

TEST_CASE("sequence_lock: a stale version cannot overwrite a newer model",
          "[sequence_lock]") {
    ReadingLock lock;
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
    ReadingLock lock;
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
    ReadingLock lock;

    auto snapshot = lock.read();
    snapshot.model.id = 1;
    TEST_ASSERT_TRUE(lock.write(snapshot) == kStored);
    TEST_ASSERT_TRUE(lock.write(snapshot) == kRejected);

    // The caller that lost the race must read again before its next write.
    auto reread = lock.read();
    reread.model.id = 3;
    TEST_ASSERT_TRUE(lock.write(reread) == kStored);

    const auto actual = lock.read();
    TEST_ASSERT_EQUAL_UINT32(3, actual.model.id);
    TEST_ASSERT_EQUAL_UINT32(4, actual.version);
}

TEST_CASE("sequence_lock: read leaves the stored model in place",
          "[sequence_lock]") {
    ReadingLock lock;
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
    using ScalarLock = SequenceLock<std::uint64_t, ConcurrentWriters::Enabled>;
    ScalarLock lock;
    auto snapshot = lock.read();
    snapshot.model = UINT64_C(0x1234567890ABCDEF);
    TEST_ASSERT_TRUE(lock.write(snapshot) == ScalarLock::WriteResult::kStored);

    const auto value = lock.read();
    TEST_ASSERT_EQUAL_UINT64(UINT64_C(0x1234567890ABCDEF), value.model);
}

// --- Single-writer form -----------------------------------------------------

TEST_CASE("sequence_lock: the single-writer form writes without rejecting",
          "[sequence_lock]") {
    SequenceLock<Reading, ConcurrentWriters::Disable> lock;
    auto snapshot = lock.read();
    snapshot.model.id = 7;

    // Void and non-blocking: the caller guarantees a single writer.
    lock.write(snapshot);

    const auto after = lock.read();
    TEST_ASSERT_EQUAL_UINT32(7, after.model.id);
    TEST_ASSERT_EQUAL_UINT32(snapshot.version + 2, after.version);
}

TEST_CASE("sequence_lock: the single-writer form advances each write by two",
          "[sequence_lock]") {
    SequenceLock<Reading, ConcurrentWriters::Disable> lock;

    auto first = lock.read();
    first.model.id = 1;
    lock.write(first);

    auto second = lock.read();
    TEST_ASSERT_EQUAL_UINT32(2, second.version);
    second.model.id = 2;
    lock.write(second);

    const auto after = lock.read();
    TEST_ASSERT_EQUAL_UINT32(4, after.version);
    TEST_ASSERT_EQUAL_UINT32(2, after.model.id);
}

TEST_CASE("sequence_lock: the single-writer form accepts an initial model",
          "[sequence_lock]") {
    const Reading initial{9, 900, {9, 9, 9, 9, 9}};
    SequenceLock<Reading, ConcurrentWriters::Disable> lock(initial);

    const auto snapshot = lock.read();
    TEST_ASSERT_EQUAL_UINT32(0, snapshot.version);
    assert_readings_equal(initial, snapshot.model);

    auto next = snapshot;
    next.model.id = 10;
    lock.write(next);
    TEST_ASSERT_EQUAL_UINT32(10, lock.read().model.id);
}

TEST_CASE("sequence_lock: the multi-writer form stores a current version",
          "[sequence_lock]") {
    ReadingLock lock;
    auto snapshot = lock.read();
    snapshot.model.id = 3;

    TEST_ASSERT_TRUE(lock.write(snapshot) == kStored);
    // The same snapshot is now stale: the successful write advanced the
    // version by two.
    TEST_ASSERT_TRUE(lock.write(snapshot) == kRejected);

    const auto after = lock.read();
    TEST_ASSERT_EQUAL_UINT32(2, after.version);
    TEST_ASSERT_EQUAL_UINT32(3, after.model.id);
}

TEST_CASE("sequence_lock: each write form is enabled by ConcurrentWriters",
          "[sequence_lock]") {
    using Single = SequenceLock<Reading, ConcurrentWriters::Disable>;
    using Multi = SequenceLock<Reading, ConcurrentWriters::Enabled>;

    static_assert(HasVoidWrite<Single>);
    static_assert(!HasResultWrite<Single>);
    static_assert(!HasVoidWrite<Multi>);
    static_assert(HasResultWrite<Multi>);
    TEST_PASS();
}

// --- Concurrent reads and writes --------------------------------------------

namespace {

struct Counter {
    std::uint32_t count = 0;
};

struct IncrementContext {
    SequenceLock<Counter, ConcurrentWriters::Enabled>* lock = nullptr;
    std::atomic<std::uint32_t>* finished = nullptr;
    std::uint32_t iterations = 0;
};

// Read, increment, and write back. A rejected write means another writer
// published first, so the snapshot must be re-read and the increment
// re-applied to the newer value.
void increment_task(void* arg) {
    auto& context = *static_cast<IncrementContext*>(arg);
    for (std::uint32_t i = 0; i < context.iterations; ++i) {
        for (;;) {
            auto snapshot = context.lock->read();
            snapshot.model.count += 1;
            using Result =
                SequenceLock<Counter, ConcurrentWriters::Enabled>::WriteResult;
            if (context.lock->write(snapshot) == Result::kStored) {
                break;
            }
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
    SequenceLock<Block, ConcurrentWriters::Enabled>* lock = nullptr;
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
        for (;;) {
            auto snapshot = context.lock->read();
            for (std::uint32_t& word : snapshot.model.words) {
                word = value;
            }
            using Result =
                SequenceLock<Block, ConcurrentWriters::Enabled>::WriteResult;
            if (context.lock->write(snapshot) == Result::kStored) {
                break;
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
        TEST_ASSERT_TRUE_MESSAGE(
            (xTaskGetTickCount() - start) < pdMS_TO_TICKS(10000),
            "timed out waiting for a writer task");
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

struct OverwriteContext {
    SequenceLock<std::string, ConcurrentWriters::Enabled>* lock = nullptr;
    SemaphoreHandle_t from_b = nullptr;
    SemaphoreHandle_t from_a = nullptr;
    std::atomic<std::uint32_t>* attempts = nullptr;
    std::atomic<std::uint32_t>* finished = nullptr;
};

// A reads the initial model and waits for B to enter its writer before it
// appends 'A'. B waits until A's overwrite has published before it tries its
// stale write, so B is forced to retry against the model A stored.
void overwrite_task_a(void* arg) {
    auto& context = *static_cast<OverwriteContext*>(arg);
    bool waited = false;
    context.lock->overwrite([&](auto& versioned_model) {
        if (!waited) {
            TEST_ASSERT_EQUAL(pdTRUE,
                              xSemaphoreTake(context.from_b, portMAX_DELAY));
            waited = true;
        }
        versioned_model.model.push_back('A');
    });

    // Give only after the write is published: B must not win the first CAS.
    TEST_ASSERT_EQUAL(pdTRUE, xSemaphoreGive(context.from_a));
    context.finished->fetch_add(1, std::memory_order_relaxed);
    vTaskDelete(nullptr);
}

void overwrite_task_b(void* arg) {
    auto& context = *static_cast<OverwriteContext*>(arg);
    bool signaled = false;
    context.lock->overwrite([&](auto& versioned_model) {
        if (!signaled) {
            TEST_ASSERT_EQUAL(pdTRUE, xSemaphoreGive(context.from_b));
            TEST_ASSERT_EQUAL(pdTRUE,
                              xSemaphoreTake(context.from_a, portMAX_DELAY));
            signaled = true;
        }
        versioned_model.model.push_back('B');
        context.attempts->fetch_add(1, std::memory_order_relaxed);
    });

    context.finished->fetch_add(1, std::memory_order_relaxed);
    vTaskDelete(nullptr);
}

}  // namespace

TEST_CASE("sequence_lock: concurrent writers do not lose an update",
          "[sequence_lock]") {
    SequenceLock<Counter, ConcurrentWriters::Enabled> lock;
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
    SequenceLock<Block, ConcurrentWriters::Enabled> lock;
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

TEST_CASE("sequence_lock: uncontentious overwrite", "[sequence_lock]") {
    SequenceLock<std::string, ConcurrentWriters::Enabled> lock;
    lock.overwrite(
        [](auto& versioned_model) { versioned_model.model.push_back('A'); });

    std::string const actual = lock.read().model;
    TEST_ASSERT_EQUAL_STRING("A", actual.c_str());
}

TEST_CASE("sequence_lock: contentious overwrite", "[sequence_lock]") {
    SequenceLock<std::string, ConcurrentWriters::Enabled> lock;
    std::atomic<std::uint32_t> attempts{0};
    std::atomic<std::uint32_t> finished{0};

    SemaphoreHandle_t from_b = xSemaphoreCreateBinary();
    SemaphoreHandle_t from_a = xSemaphoreCreateBinary();
    TEST_ASSERT_NOT_NULL(from_b);
    TEST_ASSERT_NOT_NULL(from_a);

    OverwriteContext context{&lock, from_b, from_a, &attempts, &finished};

    TaskHandle_t first_handle = nullptr;
    TaskHandle_t second_handle = nullptr;
    create_task(overwrite_task_a, "seq-overwrite-a", &context, &first_handle);
    create_task(overwrite_task_b, "seq-overwrite-b", &context, &second_handle);

    wait_for_tasks(finished, 2);

    std::string const actual = lock.read().model;
    TEST_ASSERT_EQUAL_STRING("AB", actual.c_str());
    TEST_ASSERT_TRUE_MESSAGE(attempts.load(std::memory_order_relaxed) > 1,
                             "B should have retried after a stale write");

    vSemaphoreDelete(from_a);
    vSemaphoreDelete(from_b);
}
