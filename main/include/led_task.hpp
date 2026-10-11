// LED status task: drives the LED indication from a message queue.
#pragma once

#include <cstdint>

#include "task.hpp"

namespace thermo {

// Operating states the LED task can display.
enum class LedState : std::uint8_t {
    kOff = 0,          // LEDs off.
    kBoot = 1,         // Run-mode boot flash.
    kConfig = 2,       // Configure mode: both LEDs solid ON.
    kConfig_save = 3,  // Configure mode: configuration being saved.
    kRun = 4,          // Run mode: sampling/uploading indication.
};

// Owns a FreeRTOS task that translates `LedState` messages into LED output.
// Other tasks publish state changes with `set_state()`; the LED work itself
// happens on this task.
class LedTask : public Task<LedTask> {
  public:
    // The queue element type consumed by this task.
    using Message = LedState;

    explicit LedTask(TaskParams params = {}) : Task<LedTask>(params) {}

    // Publishes a new LED state. Safe to call from any task; overwrites any
    // state not yet displayed.
    void set_state(LedState new_state) noexcept { send(new_state); }

    // Task body: waits for state messages and applies them. `[[noreturn]]`,
    // as required by `Task<Derived>`.
    [[noreturn]] void run() noexcept;

  private:
    // Applies a state change to the LEDs. Called on the LED task only.
    void on_state_changed(LedState new_state) noexcept;
};

}  // namespace thermo
