// RAII mutex and scope lock over the FreeRTOS semaphore API.
//
// `Mutex` owns one mutex: the constructor creates it (`xSemaphoreCreateMutex`
// or `xSemaphoreCreateRecursiveMutex`) and the destructor deletes it, so the
// handle is released exactly once and cannot leak. `Lock` binds the
// acquire/release pair to a scope: it locks in its constructor and releases in
// its destructor, and there is no public way to drop the mutex early.
//
// Non-blocking and timed acquisition are the static `Lock::try_lock()` and
// `Lock::try_lock_for()` factories, which return an empty `std::optional`
// instead of waiting. Exceptions are disabled in the firmware, so a failed
// blocking acquire or a contract violation aborts instead of throwing.
//
// The parts of the FreeRTOS API used here exist on the ESP-IDF `linux` target
// too, so both classes are host-testable (see the `test-espidf-cpp` project).
#pragma once

// NOLINTBEGIN
#pragma GCC diagnostic push  // NOLINT(ignored diagnostics)
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#pragma GCC diagnostic pop
// NOLINTEND

#include <chrono>
#include <optional>
#include <utility>

#include <cassert>
#include <cstdint>
#include <cstdlib>

#include "espidf_cpp/handle.hpp"

namespace thermo::espidf_cpp {

// A FreeRTOS mutex. `Kind::normal` is a plain mutex: a second take by the task
// that already holds it blocks (or fails, for the non-blocking forms).
// `Kind::recursive` may be taken several times by the same task and needs one
// release per take.
//
// Acquisition and release are private: `Lock` is the only public way to hold a
// `Mutex`, so an acquisition cannot be left unbalanced.
class Mutex {
  public:
    enum class Kind : std::uint8_t { normal, recursive };

    // Creates the mutex. Aborts if FreeRTOS cannot allocate it: without the
    // semaphore there is no lock to hand out, so there is nothing useful to
    // continue with.
    explicit Mutex(Kind kind = Kind::normal)
        : recursive_{kind == Kind::recursive} {
        SemaphoreHandle_t const handle = recursive_
                                             ? xSemaphoreCreateRecursiveMutex()
                                             : xSemaphoreCreateMutex();
        if (handle == nullptr) {
            ESP_LOGE(kTag, "mutex creation failed");
            assert(false && "FreeRTOS could not create the mutex");
            std::abort();
        }
        handle_.reset(handle);
    }

    // A mutex is neither copied nor moved: a Lock stores the Mutex's address,
    // which a move would leave dangling while the lock is held.
    Mutex(Mutex const&) = delete;
    Mutex& operator=(Mutex const&) = delete;
    Mutex(Mutex&&) = delete;
    Mutex& operator=(Mutex&&) = delete;

    ~Mutex() = default;

    [[nodiscard]] bool is_recursive() const noexcept { return recursive_; }

  private:
    friend class Lock;

    // Raw FreeRTOS handle, used by this class's own FreeRTOS calls. It remains
    // owned here (`vQueueDelete` runs when the mutex is destroyed).
    [[nodiscard]] SemaphoreHandle_t handle() const noexcept {
        return handle_.get();
    }

    // Acquires the mutex, blocking indefinitely. Aborts if the take itself
    // fails (only possible for an invalid handle, which the constructor
    // prevents).
    void lock() noexcept {
        if (!take(portMAX_DELAY)) {
            ESP_LOGE(kTag, "xSemaphoreTake failed");
            assert(false && "FreeRTOS could not take the mutex");
            std::abort();
        }
    }

    // Acquires the mutex if it is free right now. Never blocks.
    [[nodiscard]] bool try_lock() noexcept { return take(0); }

    // Acquires the mutex, waiting up to `timeout` milliseconds. Returns whether
    // the mutex was acquired before the timeout expired.
    [[nodiscard]] bool try_lock_for(
        std::chrono::milliseconds timeout) noexcept {
        return take(pdMS_TO_TICKS(static_cast<std::uint32_t>(timeout.count())));
    }

    // Releases the mutex. Aborts when the release fails, which means this task
    // did not hold the mutex (a double unlock or an unlock without a lock).
    void unlock() noexcept {
        BaseType_t const released = recursive_
                                        ? xSemaphoreGiveRecursive(handle_.get())
                                        : xSemaphoreGive(handle_.get());
        if (released != pdTRUE) {
            ESP_LOGE(kTag, "unlocking a mutex this task does not hold");
            assert(false && "FreeRTOS could not give the mutex");
            std::abort();
        }
    }

    [[nodiscard]] bool take(TickType_t ticks_to_wait) noexcept {
        BaseType_t const taken =
            recursive_ ? xSemaphoreTakeRecursive(handle_.get(), ticks_to_wait)
                       : xSemaphoreTake(handle_.get(), ticks_to_wait);
        return taken == pdTRUE;
    }

    static constexpr const char* kTag = "Mutex";

    bool recursive_{false};
    UniqueSemaphoreHandle handle_;
};

// Scope lock over a Mutex: locks in the constructor and releases in the
// destructor, so the mutex is held for the Lock's whole lifetime. It owns the
// mutex by pointer, so the Mutex must outlive it.
//
// The blocking constructor always succeeds. Non-blocking and timed acquisition
// are the static factories `try_lock()` and `try_lock_for()`, which hand back
// an empty `std::optional` when the mutex is not acquired, so a caller cannot
// hold a lock that does not exist.
class Lock {
  public:
    // Locks `mutex`, blocking until it is acquired.
    explicit Lock(Mutex& mutex) noexcept : mutex_(&mutex) { mutex_->lock(); }

    // Acquires `mutex` if it is free right now; never blocks. Returns the Lock
    // that owns it, or `std::nullopt` when the mutex is held elsewhere.
    [[nodiscard]] static std::optional<Lock> try_lock(Mutex& mutex) noexcept {
        if (!mutex.try_lock()) {
            return std::nullopt;
        }
        return Lock{mutex, AdoptLock{}};
    }

    // Acquires `mutex`, waiting up to `timeout` milliseconds. Returns the Lock
    // that owns it, or `std::nullopt` when the timeout expired first.
    [[nodiscard]] static std::optional<Lock> try_lock_for(
        Mutex& mutex, std::chrono::milliseconds timeout) noexcept {
        if (!mutex.try_lock_for(timeout)) {
            return std::nullopt;
        }
        return Lock{mutex, AdoptLock{}};
    }

    Lock(Lock const&) = delete;
    Lock& operator=(Lock const&) = delete;

    // Transfers ownership of the lock. The moved-from Lock no longer releases
    // the mutex.
    Lock(Lock&& other) noexcept
        : mutex_(std::exchange(other.mutex_, nullptr)) {}

    Lock& operator=(Lock&& other) noexcept {
        if (this != &other) {
            release();
            mutex_ = std::exchange(other.mutex_, nullptr);
        }
        return *this;
    }

    ~Lock() { release(); }

    // A Lock owns its mutex from construction until destruction or the move
    // that transfers it.
    [[nodiscard]] bool owns_lock() const noexcept { return mutex_ != nullptr; }

    explicit operator bool() const noexcept { return owns_lock(); }

    // The mutex this Lock holds, or null once the Lock has been moved from.
    [[nodiscard]] Mutex* mutex() const noexcept { return mutex_; }

  private:
    // Tag that distinguishes adopting an already-acquired mutex from the
    // public blocking constructor.
    struct AdoptLock {};

    // Wraps a mutex the static factories have already acquired.
    Lock(Mutex& mutex, AdoptLock) noexcept : mutex_(&mutex) {}

    // Releases the mutex. Private because a Lock holds its mutex for its whole
    // lifetime: only the destructor and move assignment may drop it.
    void unlock() noexcept {
        mutex_->unlock();
        mutex_ = nullptr;
    }

    // Unlocks if this Lock still owns the mutex.
    void release() noexcept {
        if (mutex_ != nullptr) {
            unlock();
        }
    }

    Mutex* mutex_{nullptr};
};

}  // namespace thermo::espidf_cpp
