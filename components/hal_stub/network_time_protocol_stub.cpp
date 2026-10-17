// Deterministic NTP stub for a simulated environment.
//
// There is no network or server to query, so the reported time is held in
// software. `init()` always succeeds; `get_utc_epoch()` returns whatever
// `set_utc_epoch()` last set (defaulting to a plausible fixed time), or
// `nullopt` when `set_sync_fails(true)` is in effect.
#include "hal/network_time_protocol.hpp"

namespace thermo::hal {

NetworkTimeProtocol::NetworkTimeProtocol() noexcept = default;

esp_err_t NetworkTimeProtocol::init() noexcept {
    return ESP_OK;
}

std::optional<std::uint32_t> NetworkTimeProtocol::get_utc_epoch(
    const std::string& server, int retry_count,
    std::uint32_t timeout_ms) noexcept {
    // Accepted for interface parity with the real implementation; unused.
    (void)server;
    (void)retry_count;
    (void)timeout_ms;
    if (sync_fails_) {
        return std::nullopt;
    }
    return epoch_;
}

void NetworkTimeProtocol::set_utc_epoch(std::uint32_t epoch) noexcept {
    epoch_ = epoch;
}

void NetworkTimeProtocol::set_sync_fails(bool fails) noexcept {
    sync_fails_ = fails;
}

}  // namespace thermo::hal
