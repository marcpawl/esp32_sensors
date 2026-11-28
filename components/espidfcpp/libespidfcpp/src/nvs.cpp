#include "espidf_cpp/nvs.hpp"

// NOLINTBEGIN
#pragma GCC diagnostic push  // NOLINT(ignored diagnostics)
#include "nvs_flash.h"
#pragma GCC diagnostic pop
// NOLINTEND

#include <atomic>
#include <mutex>

namespace thermo::espidf_cpp::detail {
namespace {

std::atomic<bool>& nvs_initialized()
{
    static std::atomic<bool> initialized{false};
    return initialized;
}

std::mutex& nvs_init_mutex()
{
    static std::mutex mutex;
    return mutex;
}

}  // namespace

// `nvs_flash_init()` is idempotent, but one successful initialization is
// shared by every Nvs mode so callers never repeat the recovery decision.
void init_nvs_flash_once()
{
    std::lock_guard<std::mutex> lock{nvs_init_mutex()};
    if (nvs_initialized().load(std::memory_order_acquire)) {
        return;
    }

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
        err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    nvs_initialized().store(true, std::memory_order_release);
}

// Tests use deinitialization to start from a fresh flash state. Clearing the
// guard makes the next init call perform the real initialization again.
void deinit_nvs_flash_once()
{
    std::lock_guard<std::mutex> lock{nvs_init_mutex()};
    if (!nvs_initialized().load(std::memory_order_acquire)) {
        return;
    }
    ESP_ERROR_CHECK(nvs_flash_deinit());
    nvs_initialized().store(false, std::memory_order_release);
}

}  // namespace thermo::espidf_cpp::detail
