#pragma once

// NOLINTBEGIN
#pragma GCC diagnostic push  // NOLINT(ignored diagnostics)
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#pragma GCC diagnostic pop
// NOLINTEND

#include <chrono>
#include <concepts>
#include <optional>
#include <type_traits>
#include <utility>

#include <cstdint>
#include <cstdlib>

#include "espidf_cpp/handle.hpp"
#include "espidf_cpp/static_string.hpp"

namespace thermo::espidf_cpp {

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

/** Create a task and execute Runner::run() in the task. */
template<typename Runner>
class Task {
  public:
    // Creates the FreeRTOS task, then starts it. The
    // task body (`Runner::run`) runs on the new task, not the caller's thread;
    // this constructor returns as soon as the task has been created.
    Task(Runner&& runner, TaskParams params)
        : params_(std::move(params)), runner_(std::move(runner)) {
        static_assert(
            std::is_nothrow_invocable_r_v<void, decltype(&Runner::run),
                                          Runner*>,
            "thermo::espidf_cpp::Task requires an accessible [[noreturn]] void "
            "run() noexcept on Runner");

        // Copy the NUL-terminated name into our own inline storage so it
        // outlives this call and FreeRTOS can safely keep a pointer to it.
        // (static_string asserts if the name exceeds kTaskNameCapacity.)
        name_.assign(params_.name);

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

  private:
    // FreeRTOS task entry point; bridges back to Derived::run().
    [[noreturn]]
    static void trampoline(void* arg) noexcept {
        auto* self = static_cast<Task*>(arg);
        self->runner_.run();
        // Runner::run() is [[noreturn]]; this is unreachable,
        [[assume(false)]];
        abort();
    }

    static constexpr const char* kTag = "task";

    TaskParams params_;
    static_string<kTaskNameCapacity> name_;
    UniqueTaskHandle handle_;

  protected:
    Runner runner_;
};

}  // namespace thermo::espidf_cpp
