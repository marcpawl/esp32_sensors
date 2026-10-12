// A small RAII CRTP base for a FreeRTOS task plus its message queue.
//
// `Task<Derived>` owns a FreeRTOS task and a single-slot queue, and calls
// `Derived::run()` on the new task. A consumer inherits from it using CRTP and
// provides the task body plus the queue's message type:
//
//   struct Job { int value; };
//
//   class Worker : public thermo::Task<Worker> {
//     public:
//       using Message = Job;  // Required: the queue element type.
//
//       explicit Worker(thermo::TaskParams params = {})
//           : thermo::Task<Worker>(params) {}
//
//       // The task body. Must be `[[noreturn]] void run() noexcept` and must
//       // be accessible to the base class (public or protected), because
//       // `Task` takes its address to enforce the signature.
//       [[noreturn]] void run() noexcept {
//         for (;;) {
//           if (auto job = receive(std::chrono::milliseconds{100})) {
//             /* use *job */
//           }
//         }
//       }
//   };
//
// Requirements on `Derived` (enforced by the static_assert below):
//   * a member type `Message` (the queue element type), and
//   * an accessible `[[noreturn]] void run() noexcept` (the task body).
//
// The queue holds a single message: sending a new message overwrites any
// message not yet received (via `xQueueOverwrite`), so the task always sees the
// most recent message rather than a backlog.
//
// `run()` must be `noexcept`; any exception escaping it would unwind across the
// FreeRTOS C frames and terminate.
//
// The class is neither copyable nor movable: the FreeRTOS task handle and
// queue handle are registered with the kernel and must not be duplicated or
// relocated.
#pragma once

#include <chrono>
#include <concepts>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <type_traits>
#include <utility>

#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "handle_deleter.hpp"
#include "thermo_static_string.hpp"

namespace thermo {

// Maximum number of characters (excluding the terminating NUL) in a task name.
// FreeRTOS copies at most configMAX_TASK_NAME_LEN bytes, so this is a safe
// upper bound.
inline constexpr std::size_t kTaskNameCapacity = 32;

// Tunable parameters for a Task. Aggregate with sensible defaults, so callers
// can override only what they need and pass it to the constructor by value.
//
// `name` is copied into the Task's own inline storage, so it need not outlive
// the call.
struct TaskParams {
    const char* name = "task";
    std::uint32_t stack_size = 4096;  // In bytes (FreeRTOS scales internally).
    UBaseType_t priority = tskIDLE_PRIORITY + 5;
};

// CRTP base for a FreeRTOS-backed task. `Derived` is the concrete task type; it
// must expose a `Message` type and a `[[noreturn]] void run() noexcept`.
// `Derived` is incomplete while it inherits from us, so anything that names a
// member of `Derived` (its `Message` type, its `run`) can only be resolved
// once `Derived` is complete. We therefore keep those references inside member
// templates / function bodies, which are only instantiated on first use (by
// which time `Derived` is complete).
template<typename Derived>
class Task {
  public:
    // The concrete task type, and the message-queue element type it consumes.
    using DerivedType = Derived;

    // Creates the message queue and the FreeRTOS task, then starts it. The
    // task body (`Derived::run`) runs on the new task, not the caller's thread;
    // this constructor returns as soon as the task has been created.
    explicit Task(TaskParams params = {}) : params_(std::move(params)) {
        // `Derived` is complete by the time any Task member is instantiated, so
        // these enforce the CRTP contract at first use (see the static_asserts
        // below, run from the constructor body).
        static_assert(has_message_type<Derived>,
                      "thermo::Task requires Derived to declare a `Message` "
                      "member type");
        static_assert(
            std::is_nothrow_invocable_r_v<void, decltype(&Derived::run),
                                          Derived*>,
            "thermo::Task requires an accessible [[noreturn]] void "
            "run() noexcept on Derived");

        // Copy the NUL-terminated name into our own inline storage so it
        // outlives this call and FreeRTOS can safely keep a pointer to it.
        // (static_string asserts if the name exceeds kTaskNameCapacity.)
        name_.assign(params_.name);

        // Single-slot queue: a new send overwrites any unread message.
        queue_.reset(xQueueCreate(1, sizeof(typename Derived::Message)));
        if (queue_ == nullptr) {
            ESP_LOGE(kTag, "xQueueCreate failed for '%s'", name_.c_str());
            abort();
        }

        TaskHandle_t handle;
        const BaseType_t created =
            xTaskCreate(&Task::trampoline, name_.c_str(), params_.stack_size,
                        this, params_.priority, &handle);
        if (created != pdPASS) {
            ESP_LOGE(kTag, "xTaskCreate failed for '%s'", name_.c_str());
            abort();
        }
        handle_.reset(handle);
    }

    ~Task() = default;

    Task(const Task&) = delete;
    Task& operator=(const Task&) = delete;
    Task(Task&&) = delete;
    Task& operator=(Task&&) = delete;

    // FreeRTOS task handle (for notifications, priority changes, ...).
    TaskHandle_t handle() const noexcept { return handle_.get(); }

    // The task name, NUL-terminated and owned by this Task.
    const char* name() const noexcept { return name_.c_str(); }

    // Message-queue handle.
    QueueHandle_t queue() const noexcept { return queue_.get(); }
  private:
    // Detects a `Message` member type on `Derived` without hard-erroring when
    // it is missing, so the static_assert can report a clear message.
    template<typename T, typename = void>
    struct has_message_type_impl : std::false_type {};

    template<typename T>
    struct has_message_type_impl<T, std::void_t<typename T::Message>>
        : std::true_type {};

    template<typename T>
    static constexpr bool has_message_type = has_message_type_impl<T>::value;

    // FreeRTOS task entry point; bridges back to Derived::run().
    static void trampoline(void* arg) noexcept {
        auto* self = static_cast<Task*>(arg);
        static_cast<Derived*>(self)->run();
        // Derived::run() is [[noreturn]]; this is unreachable, but keeps the
        // function well-formed if that contract is ever violated.
        vTaskDelete(nullptr);
    }

    static constexpr const char* kTag = "task";

    TaskParams params_;
    static_string<kTaskNameCapacity> name_;
    UniqueTaskHandle handle_;
  protected:
    UniqueQueueHandle queue_;
};

}  // namespace thermo
