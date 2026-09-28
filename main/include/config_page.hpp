// HTML rendering for the configuration UI (§5.1, §6).
//
// Kept as pure string-building functions so they can be unit-tested on the
// host without any hardware or network dependencies.
#pragma once

#include <string>
#include <vector>

#include "app_config.hpp"
#include "https_server.hpp" // ScanSnapshot

namespace thermo::config_page {

// Renders the configuration form (GET /). `errors` is populated after a failed
// POST /save so the user sees per-field messages inline.
std::string render_form(const Config& cfg, const ScanSnapshot& scan,
                        const std::vector<ValidationError>& errors);

// Renders the JSON body for GET /scan (§6.1).
std::string render_scan_json(const Config& cfg, const ScanSnapshot& scan);

// Renders the confirmation page (GET /saved).
// `warning` is non-empty when NTP sync failed (§5.3).
std::string render_saved(const Config& cfg, const std::string& warning);

// §6.2: formats saved_time as "YYYY-MM-DD HH:MM:SS UTC", or "never" for 0.
std::string format_saved_time(std::uint32_t epoch_utc);

// Minimal HTML escaping for values interpolated into the page.
std::string html_escape(const std::string& in);

} // namespace thermo::config_page
