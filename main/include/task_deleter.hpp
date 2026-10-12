
#pragma once

#include <memory>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace thermo {

struct TaskDeleter {
    void operator()(TaskHandle_t task) const {
        if (task != nullptr) {
            // Passing NULL or nullptr to vTaskDelete deletes the CURRENT
            // calling task. Explicitly checking or passing the handle ensures
            // we delete the target task.
            vTaskDelete(task);
        }
    }
};

// 2. Create a clean type alias for readability
using UniqueTaskHandle =
    std::unique_ptr<std::remove_pointer_t<TaskHandle_t>, TaskDeleter>;

}  // namespace thermo
