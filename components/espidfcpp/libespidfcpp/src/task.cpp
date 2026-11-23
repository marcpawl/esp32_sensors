#include "espidf_cpp/task.hpp"

// NOLINTBEGIN
#pragma GCC diagnostic push  // NOLINT(ignored diagnostics)
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#pragma GCC diagnostic pop
// NOLINTEND

#include <chrono>

#include <cstdint>

namespace thermo::espidf_cpp {

void vTaskDelay(std::chrono::milliseconds delay) {
    // Add one tick so wall-clock delay is not shorter than requested.
    ::vTaskDelay(pdMS_TO_TICKS(static_cast<std::uint32_t>(delay.count())) + 1U);
}

}  // namespace thermo::espidf_cpp
