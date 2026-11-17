// Tests for `espidf_cpp::Mutex` and `espidf_cpp::Lock`.
//
// `Mutex` owns a FreeRTOS mutex and keeps acquisition and release private;
// `Lock` is the only way to hold one, so a missing release is impossible.
// These tests run against the ESP-IDF `linux` target's POSIX FreeRTOS
// implementation and cover:
//
//   * creation of a normal and a recursive mutex;
//   * blocking acquisition through the Lock constructor;
//   * the static `try_lock` and `try_lock_for` factories, both when the mutex
//     is free and when it is held;
//   * recursive re-entry and the matching number of releases;
//   * releasing in a Lock's destructor;
//   * `owns_lock()` and the mutex accessor;
//   * move construction and move assignment transferring ownership.
// NOLINTBEGIN
#pragma GCC diagnostic push  // NOLINT(ignored diagnostics)
#pragma GCC diagnostic pop
// NOLINTEND

#include <chrono>
#include <optional>
#include <type_traits>
#include <utility>

#include "espidf_cpp/mutex.hpp"
#include "unity.h"

namespace {

using thermo::espidf_cpp::Lock;
using thermo::espidf_cpp::Mutex;

}  // namespace

// --- Creation ---------------------------------------------------------------

TEST_CASE("mutex: a normal mutex can be acquired and released", "[mutex]") {
    Mutex mutex;
    TEST_ASSERT_FALSE(mutex.is_recursive());
    {
        const Lock lock{mutex};
        TEST_ASSERT_TRUE(lock.owns_lock());
    }
}

TEST_CASE("mutex: a recursive mutex can be acquired recursively", "[mutex]") {
    Mutex mutex{Mutex::Kind::recursive};
    TEST_ASSERT_TRUE(mutex.is_recursive());
    {
        const Lock outer{mutex};
        const Lock inner{mutex};
        TEST_ASSERT_TRUE(inner.owns_lock());
    }
}

// --- Blocking and non-blocking acquisition ---------------------------------

TEST_CASE("lock: the constructor acquires an uncontended mutex", "[mutex]") {
    Mutex mutex;
    {
        const Lock lock{mutex};
        TEST_ASSERT_TRUE(lock.owns_lock());
        TEST_ASSERT_TRUE(static_cast<bool>(lock));
        TEST_ASSERT_EQUAL_PTR(&mutex, lock.mutex());
    }
    // The destructor released it: a fresh try_lock succeeds.
    TEST_ASSERT_TRUE(Lock::try_lock(mutex).has_value());
}

TEST_CASE("lock: try_lock acquires a free mutex and returns nullopt when held",
          "[mutex]") {
    Mutex mutex;

    {
        const Lock first{mutex};
        // A normal mutex is not re-entrant: another attempt by the same task
        // fails immediately instead of blocking.
        TEST_ASSERT_FALSE(Lock::try_lock(mutex).has_value());
    }

    const std::optional<Lock> second = Lock::try_lock(mutex);
    TEST_ASSERT_TRUE(second.has_value());
    TEST_ASSERT_TRUE(second->owns_lock());
}

TEST_CASE("lock: try_lock_for times out while the mutex is held", "[mutex]") {
    Mutex mutex;
    const Lock held{mutex};

    // A short, real wait with the mutex held by this task must time out.
    TEST_ASSERT_FALSE(
        Lock::try_lock_for(mutex, std::chrono::milliseconds{20}).has_value());
}

TEST_CASE("lock: try_lock_for acquires a free mutex", "[mutex]") {
    Mutex mutex;

    const std::optional<Lock> lock =
        Lock::try_lock_for(mutex, std::chrono::milliseconds{0});
    TEST_ASSERT_TRUE(lock.has_value());
    TEST_ASSERT_TRUE(lock->owns_lock());
}

TEST_CASE("lock: a recursive mutex permits nested Locks", "[mutex]") {
    Mutex mutex{Mutex::Kind::recursive};

    {
        const Lock outer{mutex};
        {
            const Lock inner{mutex};
            TEST_ASSERT_TRUE(inner.owns_lock());
        }
        // The inner release did not drop the outer acquisition.
        TEST_ASSERT_TRUE(outer.owns_lock());
        const std::optional<Lock> third = Lock::try_lock(mutex);
        TEST_ASSERT_TRUE(third.has_value());
    }

    TEST_ASSERT_TRUE(Lock::try_lock(mutex).has_value());
}

// --- Lock scope management --------------------------------------------------

// --- Move semantics ---------------------------------------------------------

TEST_CASE("lock: move construction transfers ownership", "[mutex]") {
    Mutex mutex;
    {
        Lock source{mutex};
        Lock moved{std::move(source)};

        // Inspecting the moved-from object is deliberate: the wrapper promises
        // it no longer owns the lock.
        // NOLINTBEGIN(clang-analyzer-cplusplus.Move, bugprone-use-after-move)
        TEST_ASSERT_FALSE(source.owns_lock());
        // NOLINTEND(clang-analyzer-cplusplus.Move, bugprone-use-after-move)
        TEST_ASSERT_TRUE(moved.owns_lock());
        // Still held by the moved-to Lock.
        TEST_ASSERT_FALSE(Lock::try_lock(mutex).has_value());
    }
    TEST_ASSERT_TRUE(Lock::try_lock(mutex).has_value());
}

TEST_CASE("lock: move assignment releases the previous lock", "[mutex]") {
    Mutex first;
    Mutex second;
    Lock target{first};
    {
        Lock source{second};
        target = std::move(source);
    }

    // The assignment released `first`; `second` is now held by `target`.
    TEST_ASSERT_TRUE(Lock::try_lock(first).has_value());
    TEST_ASSERT_FALSE(Lock::try_lock(second).has_value());
}

// --- Compile-time contract --------------------------------------------------

TEST_CASE("mutex: the wrapper is neither copyable nor movable", "[mutex]") {
    static_assert(!std::is_copy_constructible_v<Mutex>);
    static_assert(!std::is_copy_assignable_v<Mutex>);
    static_assert(!std::is_move_constructible_v<Mutex>);
    static_assert(!std::is_move_assignable_v<Mutex>);
    TEST_PASS();
}

TEST_CASE("lock: the wrapper is move-only", "[mutex]") {
    static_assert(!std::is_copy_constructible_v<Lock>);
    static_assert(!std::is_copy_assignable_v<Lock>);
    static_assert(std::is_nothrow_move_constructible_v<Lock>);
    static_assert(std::is_nothrow_move_assignable_v<Lock>);
    // Attempted acquisition reports failure through the optional.
    static_assert(
        std::is_same_v<decltype(Lock::try_lock(std::declval<Mutex&>())),
                       std::optional<Lock>>);
    TEST_PASS();
}
