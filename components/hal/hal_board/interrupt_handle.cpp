// GPIO interrupt registration owner for the development board.
//
// The header deliberately does not include the GPIO driver: the removal is the
// only ESP-IDF call this class makes, so it stays in this file and the ESP-IDF
// headers never leak to a consumer of `hal_board/interrupt_handle.hpp`.
#include "hal_board/interrupt_handle.hpp"

// NOLINTBEGIN
#pragma GCC diagnostic push  // NOLINT(ignored diagnostics)
#include "driver/gpio.h"
#include "esp_err.h"
#pragma GCC diagnostic pop
// NOLINTEND

#include <utility>

namespace thermo::hal::board {

InterruptHandle::InterruptHandle(Gpio gpio) noexcept : gpio_(gpio) {}

InterruptHandle::InterruptHandle(InterruptHandle&& other) noexcept
    : gpio_(std::exchange(other.gpio_, std::nullopt))
{
}

InterruptHandle& InterruptHandle::operator=(InterruptHandle&& other) noexcept
{
    if (this != &other) {
        // Release the pin this handle already owns before taking the source's,
        // or an overwritten handle would leave its registration behind.
        release();
        gpio_ = std::exchange(other.gpio_, std::nullopt);
    }
    return *this;
}

InterruptHandle::~InterruptHandle() noexcept
{
    release();
}

InterruptHandle::Engaged InterruptHandle::engaged() const noexcept
{
    return gpio_.has_value() ? Engaged::kEngaged : Engaged::kIdle;
}

void InterruptHandle::release() noexcept
{
    if (!gpio_.has_value()) {
        return;
    }
    ESP_ERROR_CHECK(
        gpio_isr_handler_remove(static_cast<gpio_num_t>(gpio_->as_int())));
    gpio_.reset();
}

}  // namespace thermo::hal::board
