// Hardware abstraction: network time protocol (NTP) client (§5.3, §5.4), real
// implementation for the development board.
//
// The shared vocabulary (`NetworkTimeProtocolLike`, plus the default
// retry/timeout policy) lives in the header-only `hal_common` component
// (`hal/network_time_protocol_types.hpp`); this header adds the concrete
// `NetworkTimeProtocol` class backed by the ESP-IDF SNTP client (`esp_sntp`).
// `hal_stub` provides an identical `NetworkTimeProtocol` (same no-argument
// inline `create()` factory, `init()`, `get_utc_epoch()`, plus a test-only
// `set_utc_epoch()`) so a project can link either one and include
// `<hal/network_time_protocol.hpp>` unchanged.
#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "esp_err.h"
#include "hal/network_time_protocol_types.hpp"

namespace thermo::hal {

// Queries an NTP server for the current UTC time, using the ESP-IDF SNTP
// client. The Wi-Fi interface must already be up: in Configure mode this relies
// on the connected station being available, which for a first-boot AP-only
// device may fail — in that case `get_utc_epoch()` returns `nullopt` and the
// caller reports a warning (§5.3).
class NetworkTimeProtocol {
  public:
    NetworkTimeProtocol() noexcept;

    NetworkTimeProtocol(NetworkTimeProtocol const&) = delete;
    // Movable so `create()` (and containers/factories) can hand a
    // NetworkTimeProtocol back by value without needing guaranteed copy
    // elision.
    NetworkTimeProtocol(NetworkTimeProtocol&&) noexcept = default;
    ~NetworkTimeProtocol() noexcept;
    NetworkTimeProtocol& operator=(NetworkTimeProtocol const&) = delete;
    NetworkTimeProtocol& operator=(NetworkTimeProtocol&&) = delete;

    // Builds an NTP client. Takes no parameters: the server is supplied per
    // query. Same signature for the hardware and stub implementations; call
    // init() before get_utc_epoch().
    [[nodiscard]] static NetworkTimeProtocol create() noexcept {
        return NetworkTimeProtocol();
    }

    // Prepares the SNTP client. Must be called before get_utc_epoch().
    esp_err_t init() noexcept;

    // Queries `server` for the current UTC time.
    //
    // Retries up to `retry_count` times with a `timeout_ms` timeout each
    // (§5.3). Returns Unix epoch seconds on success, `nullopt` if all attempts
    // fail. The defaults follow §5.3 ("retry up to 3 times ... e.g. 5 s each");
    // the firmware overrides them with its own timing policy.
    [[nodiscard]] std::optional<std::uint32_t> get_utc_epoch(
        const std::string& server, int retry_count = kNtpDefaultRetryCount,
        std::uint32_t timeout_ms = kNtpDefaultTimeoutMs) noexcept;

  private:
    // Whether init() has succeeded; get_utc_epoch() asserts on it.
    bool initialized_ = false;
};

static_assert(
    NetworkTimeProtocolLike<NetworkTimeProtocol>,
    "NetworkTimeProtocol must satisfy the NetworkTimeProtocolLike concept");

}  // namespace thermo::hal
