// Interactive console task for the `donny` linux emulation.
//
// `donny` runs the pin-free `hal_stub` board on the host, so nothing ever
// changes the hardware: a test driver has to. This task gives it a channel.
// Each line read from stdin is a JSON document describing the *board's state*;
// the task applies it to the `BoardStub`, then writes the resulting state back
// as a single line of JSON.
//
// Round-tripping is idempotent: feeding the task's own output back in changes
// nothing and produces byte-identical output. That is what makes the driver's
// job simple — it can read the current state, edit one field, and write it
// back, without having to know which fields exist.
//
// The two fields that cannot be set through the board's interface —
// `wifi.initialized` (only `init()` sets it) and `last_deep_sleep_s` (only
// `deep_sleep_start()` sets it) — are reported but ignored on input, so they
// round-trip too.
//
// Like the rest of the HAL-less layers, the task is split the same way
// `LedTask` is: `ConsoleRunner` holds the behaviour and the board, and
// `ConsoleTask` is the FreeRTOS task that runs it.
#pragma once

// NOLINTBEGIN
#pragma GCC diagnostic push  // NOLINT(ignored diagnostics)
#pragma GCC diagnostic pop
// NOLINTEND

#include <memory>

#include "espidf_cpp/task.hpp"
#include "hal/board_stub.hpp"

namespace thermo {

/** Reads JSON lines from stdin, applies them to the board, and echoes the
 * board's state back as one JSON line.
 *
 * `run()` never returns: it is a `[[noreturn]]` FreeRTOS task body. When stdin
 * reaches EOF (the driver closed the pipe) it stops reading and idles, so the
 * rest of the application keeps running.
 */
class ConsoleRunner {
  public:
    // Shares `board` with the caller (never null): the task mutates the same
    // board the application reads from.
    explicit ConsoleRunner(std::shared_ptr<hal::BoardStub> board) noexcept;

    ConsoleRunner(ConsoleRunner const&) = delete;
    ConsoleRunner(ConsoleRunner&&) = default;
    ~ConsoleRunner() = default;
    ConsoleRunner& operator=(ConsoleRunner const&) = delete;
    ConsoleRunner& operator=(ConsoleRunner&&) = delete;

    // Task body. Lives on the console task, not the caller's thread.
    [[noreturn]] void run() noexcept;

  private:
    std::shared_ptr<hal::BoardStub> board_;
};

/** FreeRTOS task that runs a `ConsoleRunner` against the stub board.
 *
 * Started before the `Application` is constructed, so a driver can already
 * change the board while the application boots.
 */
class ConsoleTask : public espidf_cpp::Task<ConsoleRunner> {
  public:
    // Shares `board` with the caller and starts the task.
    explicit ConsoleTask(std::shared_ptr<hal::BoardStub> board);

    ConsoleTask(ConsoleTask const&) = delete;
    ConsoleTask(ConsoleTask&&) = delete;
    ~ConsoleTask() = default;
    ConsoleTask& operator=(ConsoleTask const&) = delete;
    ConsoleTask& operator=(ConsoleTask&&) = delete;
};

}  // namespace thermo
