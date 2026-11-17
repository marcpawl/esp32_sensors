#include "espidf_cpp/mutex.hpp"

// NOLINTBEGIN
#pragma GCC diagnostic push  // NOLINT(ignored diagnostics)
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#pragma GCC diagnostic pop
// NOLINTEND

#include <cassert>
#include <cstdlib>

namespace thermo::espidf_cpp {

Mutex::Mutex(Kind kind) : recursive_{kind == Kind::recursive} {
    SemaphoreHandle_t const handle =
        recursive_ ? xSemaphoreCreateRecursiveMutex() : xSemaphoreCreateMutex();
    if (handle == nullptr) {
        ESP_LOGE(kTag, "mutex creation failed");
        assert(false && "FreeRTOS could not create the mutex");
        std::abort();
    }
    handle_.reset(handle);
}

void Mutex::unlock() noexcept {
    BaseType_t const released = recursive_
                                    ? xSemaphoreGiveRecursive(handle_.get())
                                    : xSemaphoreGive(handle_.get());
    if (released != pdTRUE) {
        ESP_LOGE(kTag, "unlocking a mutex this task does not hold");
        assert(false && "FreeRTOS could not give the mutex");
        std::abort();
    }
}

}  // namespace thermo::espidf_cpp
