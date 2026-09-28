// LED status indication (§7).
#pragma once

#include <cstdint>

#include "esp_err.h"

namespace thermo {

enum class TxState : std::uint8_t {
    kTxOk = 0,         // Transmission permitted.
    kTxInhibited = 1,  // Low battery; buffering only.
};

// Drives the red/green LEDs. Semantics map 1:1 to the table in §7.
class LedManager {
  public:
    esp_err_t init();

    // §7 "Configure mode": both LEDs solid ON.
    void set_configure_mode();

    // §7 "Run - boot": both LEDs 200 ms flash.
    void boot_flash();

    // §7 "Run - sampling": both OFF.
    void set_sampling();

    // §7 "Run - uploading": green ON, red OFF.
    void set_uploading();

    // §7 "Run - upload OK": green 200 ms pulse then off.
    void upload_ok_pulse();

    // §7 "Run - TX error": red ON, green OFF.
    void set_tx_error();

    // §7 "Run - recovered": green 1 s solid, then OFF.
    void recovered_pulse();

    // §7 "Fatal error": red blinks at 0.5 Hz.
    void set_fatal();

    // §7 "Run - TX inhibited": red 500 ms blink every 10 s. Non-blocking;
    // call `poll()` from the run loop to advance the blink phase.
    void set_inhibited();
    void poll(std::uint32_t now_ms);

    void all_off();

  private:
    void set_green(bool on);
    void set_red(bool on);

    bool red_on_ = false;
    bool green_on_ = false;
    bool inhibited_ = false;
    bool fatal_ = false;
    std::uint32_t last_blink_ms_ = 0;
    bool blink_phase_ = false;
};

} // namespace thermo
