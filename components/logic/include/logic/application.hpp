// Application entry point (§3).
//
// Part of the `logic` component: deciding what the device does with its power
// budget — read the mode-sense poles, then dispatch to Configure, Off, or Run —
// is board-independent application behaviour. It belongs next to the mode
// vocabulary (`logic/mode.hpp`) and the pole mapping
// (`logic/mode_selection.hpp`) rather than in the firmware's `app_main`, which
// stays a thin C-linkage shim.
//
// Header-only for now: the body is empty, so it is defined `inline` here rather
// than in a `.cpp`. `app_main` does not call it yet.
#pragma once

// NOLINTBEGIN
#pragma GCC diagnostic push  // NOLINT(ignored diagnostics)
#pragma GCC diagnostic pop
#include "../../../hal_common/include/hal/board_types.hpp"

// NOLINTEND

namespace thermo {

// Runs the application: reads the two mode-sense poles and dispatches to
// Configure mode, the Off position, or the Run-mode loop (§3). Called once
// from `app_main`.
template<hal::BoardLike Board>
class Application {
  public:
    explicit Application(std::shared_ptr<Board> board);
    Application(Application const&) = delete;
    Application(Application&&) = delete;
    ~Application() = default;
    Application& operator=(Application const&) = delete;
    Application& operator=(Application&&) = delete;

    void application_main(void);

  private:
    std::shared_ptr<Board> board_;
};

template<hal::BoardLike Board>
Application<Board>::Application(std::shared_ptr<Board> board)
    : board_(std::move(board)) {}

template<hal::BoardLike Board>
void Application<Board>::application_main(void) {
    while (true) {
        vTaskDelay(1000);
    }
}

}  // namespace thermo
