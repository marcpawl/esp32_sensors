#pragma once

// NOLINTBEGIN
#pragma GCC diagnostic push  // NOLINT(ignored diagnostics)
#include "driver/gpio.h"
#pragma GCC diagnostic pop
// NOLINTEND

#include "espidf_cpp/switch.hpp"

#include "hal/board_types.hpp"

// #if defined(CONFIG_IDF_TARGET_LINUX)
// #include "hal_stub/board_stub.hpp"
// #else
// #include "hal_board/board.hpp"
// #endif

namespace thermo::logic {

// #if defined(CONFIG_IDF_TARGET_LINUX)
// using BoardBase = thermo::hal::stub::BoardStub;
// #else
// using BoardBase = thermo::hal::board::Board;
// #endif

// TODO BoardBase is being defined while we are in transition,
// it will be removed.
// TODO unit test
class Board
// : public BoardBase
{
  public:
    Board()
        // : BoardBase({}),
        : config_switch_(espidf_cpp::Gpio{GPIO_NUM_1}),
          run_switch_(espidf_cpp::Gpio{GPIO_NUM_2}) {
        (void)gpio_install_isr_service(0);
    }

    Board(Board const&) = delete;
    Board(Board&&) = delete;
    ~Board() = default;
    Board& operator=(Board const&) = delete;
    Board& operator=(Board&&) = delete;

    using Switch = espidf_cpp::Switch;

    // The Configure-mode sense switch (one pole of the 3-position switch).
    [[nodiscard]] Switch& config_switch() noexcept { return config_switch_; }

    [[nodiscard]] espidf_cpp::Switch const& config_switch() const noexcept {
        return config_switch_;
    }

    // The Run-mode sense switch (one pole of the 3-position switch).
    [[nodiscard]] espidf_cpp::Switch& run_switch() noexcept {
        return run_switch_;
    }

    [[nodiscard]] Switch const& run_switch() const noexcept {
        return run_switch_;
    }


  private:
    Switch config_switch_;
    Switch run_switch_;
};

}  // namespace thermo::logic
