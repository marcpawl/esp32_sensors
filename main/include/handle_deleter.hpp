
#pragma once

#include <memory>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

namespace thermo {

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

// 2. Create a clean type alias for readability
using UniqueTaskHandle =
    std::unique_ptr<std::remove_pointer_t<TaskHandle_t>,
                    HandleDeleter<TaskHandle_t, vTaskDelete>>;

using UniqueQueueHandle =
    std::unique_ptr<std::remove_pointer_t<QueueHandle_t>,
                    HandleDeleter<QueueHandle_t, vQueueDelete>>;

}  // namespace thermo
