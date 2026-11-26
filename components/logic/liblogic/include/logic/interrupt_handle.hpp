// RAII ownership of a GPIO interrupt registration in the logic layer.
//
// Every successful `gpio_isr_handler_add()` needs a matching
// `gpio_isr_handler_remove()`. Forgetting it leaves an ISR pointing at state
// that no longer exists, which is why the removal is bound to the lifetime of
// the object that installed it. A default-constructed or moved-from handle
// owns no pin and does nothing when it is destroyed.
#pragma once

// NOLINTBEGIN
#pragma GCC diagnostic push  // NOLINT(ignored diagnostics)
#pragma GCC diagnostic pop
// NOLINTEND

#include <optional>

#include <cstdint>

#include "hal/gpio.hpp"

namespace thermo::logic {

class InterruptHandle {
  public:
    // Whether the handle owns a pin. Named rather than `bool`, following the
    // repository's rule for members and results.
    enum class Engaged : std::uint8_t {
        kIdle = 0,
        kEngaged,
    };

    // Owns nothing: destroying this handle does not touch a GPIO.
    InterruptHandle() noexcept = default;

    // Owns `gpio`: destroying the handle removes that pin's ISR handler.
    explicit InterruptHandle(hal::Gpio gpio) noexcept;

    // A registration must have exactly one owner, so copying is not allowed.
    InterruptHandle(InterruptHandle const&) = delete;
    InterruptHandle& operator=(InterruptHandle const&) = delete;

    // Moving transfers ownership and leaves the source empty, so only the
    // destination removes the handler.
    InterruptHandle(InterruptHandle&& other) noexcept;
    InterruptHandle& operator=(InterruptHandle&& other) noexcept;

    // Removes the handler if this handle owns one.
    ~InterruptHandle() noexcept;

    // Whether this handle owns a pin.
    [[nodiscard]] Engaged engaged() const noexcept;

    // Removes the handler and gives up ownership. Idempotent.
    void release() noexcept;

  private:
    std::optional<hal::Gpio> gpio_;
};

}  // namespace thermo::logic
