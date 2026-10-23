// Hardware abstraction: WiFi radio (§3.2, §3.9), deterministic stub for the
// host tests.
//
// The shared WiFi vocabulary (`AccessPointConfig`, `StationConfig`,
// `WifiRadioLike`) lives in the header-only `hal_common` component
// (`hal/wifi_types.hpp`); this header adds the software-driven `WifiRadio`
// class. `hal_board` provides the real, driver-backed `WifiRadioBoard` plus the
// same `WifiRadio` alias, so a project can link either one and include
// `<hal/wifi.hpp>` unchanged.
//
// This stub replaces the ESP-IDF WiFi driver where there is no radio (or it
// must not be touched). It models the radio's behaviour in software: the AP
// and the station keep their own state and can run at the same time, starting
// one never stops the other, and the accessors below let host
// tests assert exactly what was asked of the radio. No networking happens —
// the AP address is a fixed placeholder.
//
// The radio is semantics-free, as the concept requires: it knows how to bring
// an access point or a station up and down, but not when to. Retry policy
// (e.g. periodically re-joining the station in Configure mode, §3.2) belongs
// to the caller; the stub records the last request verbatim.
#pragma once

#include <cstdint>
#include <optional>

#include "esp_err.h"
#include "hal/wifi_types.hpp"

namespace thermo::hal {

// Represents the WiFi radio with all of its state held in software.
class WifiRadioStub {
  public:
    WifiRadioStub() noexcept = default;

    WifiRadioStub(WifiRadioStub const&) = delete;
    // Movable so `create()` (and containers/factories) can hand a radio back
    // by value without needing guaranteed copy elision.
    WifiRadioStub(WifiRadioStub&&) noexcept = default;
    ~WifiRadioStub() = default;
    WifiRadioStub& operator=(WifiRadioStub const&) = delete;
    WifiRadioStub& operator=(WifiRadioStub&&) = delete;

    // Builds a radio. Same signature for the hardware and stub
    // implementations; call init() before any other method.
    [[nodiscard]] static WifiRadioStub create() noexcept {
        return WifiRadioStub();
    }

    // Marks the stub ready; always succeeds. Every other method returns
    // `ESP_ERR_INVALID_STATE` until it has been called, matching the real
    // implementation.
    esp_err_t init() noexcept;

    // Records `config` and brings the AP up. Succeeds (and re-records the
    // configuration) if the AP is already up.
    esp_err_t start_ap(AccessPointConfig const& config) noexcept;

    // Brings the AP down. Succeeds if it was not up. The station is
    // unaffected.
    esp_err_t stop_ap() noexcept;

    // Records `config` and brings the station up. Succeeds (and re-records the
    // configuration) if it is already up. An empty SSID is rejected with
    // `ESP_ERR_INVALID_ARG`; the station state is left unchanged.
    esp_err_t start_station(StationConfig const& config) noexcept;

    // Brings the station down. Succeeds if it was not up. The AP is
    // unaffected.
    esp_err_t stop_station() noexcept;

    // --- Observation (stub only; not part of the concept) ------------------

    // True once init() has succeeded.
    [[nodiscard]] bool initialized() const noexcept;

    // True while the AP is up.
    [[nodiscard]] bool ap_running() const noexcept;

    // True while the station is up.
    [[nodiscard]] bool station_running() const noexcept;

    // The configuration most recently passed to start_ap(), if any.
    [[nodiscard]] std::optional<AccessPointConfig> const& ap_config()
        const noexcept;

    // The configuration most recently passed to start_station(), if any.
    [[nodiscard]] std::optional<StationConfig> const& station_config()
        const noexcept;

    // The address a client would reach the AP at (fixed placeholder).
    static constexpr std::uint32_t kApIpv4 = 0xC0A80401;  // 192.168.4.1

  private:
    bool initialized_ = false;
    bool ap_running_ = false;
    bool station_running_ = false;
    std::optional<AccessPointConfig> ap_config_;
    std::optional<StationConfig> station_config_;
};

// The radio implementation linked into this project.
using WifiRadio = WifiRadioStub;

static_assert(WifiRadioLike<WifiRadio>,
              "WifiRadio must satisfy the WifiRadioLike concept");

}  // namespace thermo::hal
