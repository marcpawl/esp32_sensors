// RAII owners for FreeRTOS handles.
//
// FreeRTOS hands out raw handles that must be released by a separate call
// (`vTaskDelete`, `vQueueDelete`, ...). `HandleDeleter` turns any such handle
// and cleanup function into a deleter usable with `std::unique_ptr`, so the
// handle is released exactly once and leaks are avoided (including on early
// returns).
#pragma once

// NOLINTBEGIN
#pragma GCC diagnostic push  // NOLINT(ignored diagnostics)
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#pragma GCC diagnostic pop
// NOLINTEND

#include <memory>

namespace thermo::espidf_cpp {

template<typename Handle_t, auto CleanupFn>
struct HandleDeleter {
    // Constexpr deleter, constant-evaluable on the null path only (the
    // FreeRTOS `CleanupFn` is not constexpr), which is enough to allow a
    // `unique_ptr` holding a null handle to be destroyed at compile time.
    constexpr void operator()(Handle_t handle) const noexcept {
        if (handle != nullptr) {
            CleanupFn(handle);
        }
    }
};

using UniqueTaskHandle =
    std::unique_ptr<std::remove_pointer_t<TaskHandle_t>,
                    HandleDeleter<TaskHandle_t, vTaskDelete>>;

using UniqueQueueHandle =
    std::unique_ptr<std::remove_pointer_t<QueueHandle_t>,
                    HandleDeleter<QueueHandle_t, vQueueDelete>>;

}  // namespace thermo::espidf_cpp
