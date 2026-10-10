// Reads the 3-position ON-OFF-ON switch (§2.3, §3).
#pragma once

#include "esp_err.h"
#include "mode.hpp"

namespace thermo {

class ModeController {
  public:
    ModeController(int config_gpio, int run_gpio);

    esp_err_t init();

    // Samples the switch. Reboot if there is no definitive reading.
    Mode read() const;

  private:
  /** Count of the button state when read repeatedly. */
  struct ReadCounts {
    int attempts_ = 0;
    int config_ = 0;
    int run_ = 0;
  };

  void read_once(ReadCounts& counts) const;

  /** Read the button many times, returning the number
   *  of times each button state was read.
   */
  ReadCounts read_many_times() const;

  int const config_gpio_;
  int const run_gpio_;

};

}  // namespace thermo
