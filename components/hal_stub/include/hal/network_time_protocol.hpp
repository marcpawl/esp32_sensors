// Hardware abstraction: network time protocol (NTP) client (§5.3, §5.4),
// deterministic stub for a simulated environment.
//
// The shared vocabulary (`NetworkTimeProtocolLike`, plus the default
// retry/timeout policy) lives in the header-only `hal_common` component
// (`hal/network_time_protocol_types.hpp`); this header adds the software-driven
// `NetworkTimeProtocol` class. `hal_board` provides an identical
// `NetworkTimeProtocol` (same no-argument inline `create()` factory, `init()`,
// `get_utc_epoch()`), so a project can link either one and include
// `<hal/network_time_protocol.hpp>` unchanged.
//
// This stub replaces the SNTP-backed `hal_board` implementation where there is
// no network or NTP server. The reported time is held in software and set with
// `set_utc_epoch()`; `set_sync_fails()` lets a test simulate an unreachable
// server, so the failure path can be exercised without a network.
#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "esp_err.h"
#include "hal/network_time_protocol_types.hpp"

namespace thermo::hal {

// Represents the NTP client with its reported time held in software.
class NetworkTimeProtocol {
  public:
    NetworkTimeProtocol() noexcept;

    NetworkTimeProtocol(NetworkTimeProtocol const&) = delete;
    // Movable so `create()` (and containers/factories) can hand a
    // NetworkTimeProtocol back by value without needing guaranteed copy
    // elision.
    NetworkTimeProtocol(NetworkTimeProtocol&&) noexcept = default;
    ~NetworkTimeProtocol() noexcept = default;
    NetworkTimeProtocol& operator=(NetworkTimeProtocol const&) = delete;
    NetworkTimeProtocol& operator=(NetworkTimeProtocol&&) = delete;

    // Builds a stub NTP client. Takes no parameters: the server is supplied per
    // query. Same signature for the hardware and stub implementations; call
    // init() before get_utc_epoch().
    [[nodiscard]] static NetworkTimeProtocol create() noexcept {
        return NetworkTimeProtocol();
    }

    // No-op for the stub (there is nothing to configure); always succeeds.
    esp_err_t init() noexcept;

    // Returns the time previously set with `set_utc_epoch()`, or `nullopt`
    // when the stub is set to simulate a failed sync. The `server`,
    // `retry_count`, and `timeout_ms` arguments are accepted for interface
    // parity with the real implementation and ignored.
    [[nodiscard]] std::optional<std::uint32_t> get_utc_epoch(
        const std::string& server, int retry_count = kNtpDefaultRetryCount,
        std::uint32_t timeout_ms = kNtpDefaultTimeoutMs) noexcept;

    // Sets the UTC time (Unix epoch seconds) reported by subsequent
    // `get_utc_epoch()` calls. Intended for a simulated environment.
    void set_utc_epoch(std::uint32_t epoch) noexcept;

    // Makes subsequent `get_utc_epoch()` calls simulate an unreachable server
    // (returning `nullopt`) when `fails` is true. Intended for a simulated
    // environment, so the failure path (§5.3) can be exercised.
    void set_sync_fails(bool fails) noexcept;

  private:
    // A plausible fixed time (2023-11-14 ~22:13 UTC) so callers see a realistic
    // value without configuring anything.
    static constexpr std::uint32_t kDefaultEpoch = 1700000000;
    std::uint32_t epoch_ = kDefaultEpoch;
    bool sync_fails_ = false;
};

static_assert(
    NetworkTimeProtocolLike<NetworkTimeProtocol>,
    "NetworkTimeProtocol must satisfy the NetworkTimeProtocolLike concept");

}  // namespace thermo::hal
