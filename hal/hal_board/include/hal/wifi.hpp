// Hardware abstraction: WiFi radio (§3.2, §3.9), real ESP-IDF implementation
// for the development board.
//
// The shared WiFi vocabulary (`AccessPointConfig`, `StationConfig`,
// `WifiRadioLike`) lives in the header-only `hal_common` component
// (`hal/wifi_types.hpp`); this header adds the concrete, driver-backed
// `WifiRadioBoard` class plus the `WifiRadio` alias the rest of the firmware
// uses. `hal_stub` provides a software-driven `WifiRadio` with the same public
// interface, so a project can link either one and include `<hal/wifi.hpp>`
// unchanged.
//
// The radio is semantics-free: it knows how to bring an access point or a
// station up and down, but not when to. The two interfaces are independent —
// Configure mode (§3.2) may run both at once — and the caller owns the policy
// (e.g. retrying the station join; the driver retries on its own, but only
// while the station is started).
#pragma once

// NOLINTBEGIN
#pragma GCC diagnostic push  // NOLINT(ignored diagnostics)
#include "esp_err.h"
#pragma GCC diagnostic pop
// NOLINTEND

#include "hal/wifi_types.hpp"

namespace thermo::hal {

// The WiFi radio: one ESP-IDF WiFi driver instance driving the SoftAP and the
// station interface.
//
// `init()` must be called once before anything else; it installs the WiFi
// driver and the default AP/station interfaces. Every other method returns
// `ESP_ERR_INVALID_STATE` if called first.
//
// The driver only runs while at least one interface is enabled: `stop_ap()`
// and `stop_station()` stop the driver once neither is left, so an idle radio
// draws no power.
class WifiRadioBoard {
  public:
    // `netif`/event start-up and the driver instance are process-wide, so one
    // radio is the supported configuration (a second instance shares them).
    WifiRadioBoard() noexcept = default;

    WifiRadioBoard(WifiRadioBoard const&) = delete;
    // Movable so `create()` (and containers/factories) can hand a radio back
    // by value without needing guaranteed copy elision.
    WifiRadioBoard(WifiRadioBoard&&) noexcept = default;
    ~WifiRadioBoard() = default;
    WifiRadioBoard& operator=(WifiRadioBoard const&) = delete;
    WifiRadioBoard& operator=(WifiRadioBoard&&) = delete;

    // Builds a radio. Same signature for the hardware and stub
    // implementations; call init() before any other method.
    [[nodiscard]] static WifiRadioBoard create() noexcept {
        return WifiRadioBoard();
    }

    // Initializes netif + the default event loop, creates the default AP and
    // station interfaces, and installs the WiFi driver. Idempotent; returns
    // the first error without changing state on failure.
    esp_err_t init() noexcept;

    // Configures and starts the SoftAP from `config`. If the AP is already
    // running it is stopped and restarted with the new configuration.
    esp_err_t start_ap(AccessPointConfig const& config) noexcept;

    // Stops the SoftAP. Succeeds if the AP was not running. The station, if
    // running, is unaffected.
    esp_err_t stop_ap() noexcept;

    // Configures the station from `config` and starts it connecting. If the
    // station is already running it is disconnected and restarted with the
    // new configuration. An empty SSID is rejected with `ESP_ERR_INVALID_ARG`
    // (the caller decides not to start it at all).
    esp_err_t start_station(StationConfig const& config) noexcept;

    // Disconnects and stops the station interface. Succeeds if it was not
    // running. The AP, if running, is unaffected.
    esp_err_t stop_station() noexcept;

  private:
    // Starts/updates the driver for the enabled interfaces, or stops it when
    // none are enabled.
    esp_err_t apply_interfaces() noexcept;

    bool initialized_ = false;
    bool ap_enabled_ = false;
    bool station_enabled_ = false;
};

// The radio implementation linked into this project.
using WifiRadio = WifiRadioBoard;

static_assert(WifiRadioLike<WifiRadio>,
              "WifiRadio must satisfy the WifiRadioLike concept");

}  // namespace thermo::hal
