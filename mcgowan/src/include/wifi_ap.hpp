// Wi-Fi Access Point for Configure mode (§3.2).
#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "app_config.hpp"
#include "esp_err.h"

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
