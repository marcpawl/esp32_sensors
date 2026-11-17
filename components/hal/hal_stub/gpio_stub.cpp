// The one GPIO C entry point the `linux` target needs.
//
// ESP-IDF's `esp_driver_gpio` registers an empty component on the `linux`
// target, so neither its sources nor its headers are available there.
// `BoardStub` installs the GPIO ISR service in its constructor just like the
// hardware `Board`, so this file supplies the symbol. The stub switches have no
// real interrupts, so installing the service is a no-op that succeeds.
// NOLINTBEGIN
#pragma GCC diagnostic push  // NOLINT(ignored diagnostics)
#include "esp_err.h"
#pragma GCC diagnostic pop

// NOLINTEND

extern "C" esp_err_t gpio_install_isr_service(int intr_alloc_flags) {
    (void)intr_alloc_flags;
    return ESP_OK;
}
