// Application entry point (§3).
//
// Part of the `liblogic` component: deciding what the device does with its
// power budget — read the mode-sense poles, then dispatch to Configure, Off,
// Run, or handle an invalid pole pair — is board-independent application
// behaviour. It belongs next to the mode
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
// NOLINTEND

#include "logic/board.hpp"
#include "logic/mode.hpp"

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

    [[noreturn]]
    void application_main(void);

  private:
    // Update #mode_ based on the current switch states.
    void update_mode();

    void initialize_state(void);
    [[noreturn]] void configure_state();
    [[noreturn]] void run_state();
    [[noreturn]] void off_state();

    Board& board_;
    Mode mode_{Mode::kInvalid};
};

}  // namespace thermo::logic
