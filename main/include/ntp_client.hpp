// NTP time acquisition used by the save handler (§5.3, §5.4).
#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace thermo {

// Attempts to obtain the current UTC time from `server`.
//
// Retries up to kNtpRetryCount times with a kNtpTimeoutMs timeout each (§5.3).
// Returns Unix epoch seconds on success, nullopt if all attempts fail.
//
// Starts the SNTP client on demand; the Wi-Fi interface must already be up.
std::optional<std::uint32_t> ntp_get_utc_epoch(const std::string& server);

} // namespace thermo
