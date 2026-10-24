// Wi-Fi Access Point for Configure mode (§3.2).
#pragma once

// NOLINTBEGIN
#pragma GCC diagnostic push  // NOLINT(ignored diagnostics)
#include "esp_err.h"
#pragma GCC diagnostic pop
// NOLINTEND

#include <optional>
#include <string>

#include <cstdint>

#include "app_config.hpp"

namespace thermo {

// RAII Wi-Fi station connection for Run mode / NTP sync. Stubbed minimally
// here since Run mode is stubbed.
class WifiAp {
  public:
    // Brings up SoftAP using the configured AP SSID/password.
    esp_err_t start(const Config& cfg);

    // Returns the AP's IP address (typically 192.168.4.1) once started.
    std::string ip_address() const;

  private:
    bool started_ = false;
};

}  // namespace thermo
