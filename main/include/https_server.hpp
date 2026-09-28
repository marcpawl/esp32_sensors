// HTTPS configuration server (§6).
#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "app_config.hpp"
#include "esp_err.h"
#include "sensor.hpp"

namespace thermo {

// Snapshot of live sensor/battery data exposed via GET /scan (§6.1).
struct ScanSnapshot {
    std::uint32_t battery_mv = 0;
    std::vector<SensorReading> sensors;
};

// Callback used by the server to obtain a fresh /scan snapshot on demand.
using ScanProvider = std::function<ScanSnapshot()>;

// Callback invoked on POST /save with the validated candidate configuration.
// Returns additional warning text to display on /saved (e.g. NTP failure), or
// nullopt when there were no warnings.
using SaveHandler = std::function<std::optional<std::string>(const Config&)>;

// Serves the Configure-mode UI over TLS on port 443 (§6).
class HttpsServer {
  public:
    HttpsServer(Config& cfg, ScanProvider scan_provider,
                SaveHandler save_handler);

    esp_err_t start();
    esp_err_t stop();

  private:
    Config& cfg_;
    ScanProvider scan_provider_;
    SaveHandler save_handler_;
    void* handle_ = nullptr; // httpd_handle_t
};

} // namespace thermo
