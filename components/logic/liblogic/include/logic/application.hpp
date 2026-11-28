// Application entry point (§3).
//
// Part of the `liblogic` component: deciding what the device does with its
// power budget — read the mode-sense poles, then dispatch to Configure, Off, or
// Run — is board-independent application behaviour. It belongs next to the mode
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
#include "logic/board.hpp"

// NOLINTEND

namespace thermo::logic {

// Runs the application: reads the two mode-sense poles and dispatches to
// Configure mode, the Off position, or the Run-mode loop (§3). Called once
// from `app_main`.
class Application {
  private:
    constexpr static char const* const kTag = "Application";

  public:
    explicit Application(Board& board);
    Application(Application const&) = delete;
    Application(Application&&) = delete;
    ~Application() = default;
    Application& operator=(Application const&) = delete;
    Application& operator=(Application&&) = delete;

    void application_main(void);

  private:
    Board& board_;
};

}  // namespace thermo::logic
