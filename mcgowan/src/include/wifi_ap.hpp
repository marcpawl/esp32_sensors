// Wi-Fi Access Point for Configure mode (§3.2).
#pragma once

// NOLINTBEGIN
#pragma GCC diagnostic push  // NOLINT(ignored diagnostics)
#include "esp_err.h"
#pragma GCC diagnostic pop
// NOLINTEND

#include <optional>

#include <cstddef>
#include <cstdint>

#include "app_config.hpp"
#include "utility/static_string.hpp"

namespace thermo {

// Longest IPv4 dotted-quad text: "255.255.255.255" is 15 characters.
inline constexpr std::size_t kIpAddressTextCapacity = 15;
using IpAddressText = utility::static_string<kIpAddressTextCapacity>;

// RAII Wi-Fi station connection for Run mode / NTP sync. Stubbed minimally
// here since Run mode is stubbed.
class WifiAp {
  public:
    // Brings up SoftAP using the configured AP SSID/password.
    esp_err_t start(const Config& cfg);

    // Returns the AP's IP address (typically 192.168.4.1) once started.
    [[nodiscard]] IpAddressText ip_address() const;

  private:
    bool started_ = false;
};

}  // namespace thermo
