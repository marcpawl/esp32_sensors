// Shared, implementation-free types for the network time protocol (NTP) client
// (§5.3, §5.4).
//
// This header is the *single* definition of the NTP vocabulary — the
// `NetworkTimeProtocolLike` concept, plus the default retry/timeout policy the
// concept's `get_utc_epoch()` uses — shared by both implementations:
//
//   - `hal_board` — real implementation that drives the ESP-IDF SNTP client
//     (`esp_sntp`), which needs the Wi-Fi interface to be up.
//   - `hal_stub`  — deterministic stub used under the simulated environment and
//     host tests; the reported time is held in software.
//
// Each implementation provides its own `hal/network_time_protocol.hpp` that
// includes this header and adds the concrete `NetworkTimeProtocol` class, so
// there is exactly one definition of the shared contract and the two
// implementations cannot drift. (A project still links exactly one of
// `hal_board` / `hal_stub`, but both `REQUIRES hal_common`.)
#pragma once

#include <concepts>
#include <cstdint>
#include <optional>
#include <string>
#include <type_traits>

#include "esp_err.h"

namespace thermo::hal {

// Default NTP retry/timeout policy (§5.3: "retry up to 3 times with a short
// timeout, e.g. 5 s each"). These are the defaults for `get_utc_epoch()`; the
// caller may override them (the firmware passes its own `timing::kNtp*` values
// so the policy stays in the firmware, not in the HAL).
inline constexpr int kNtpDefaultRetryCount = 3;
inline constexpr std::uint32_t kNtpDefaultTimeoutMs = 5000;

// Concept: a network-time-protocol type that exposes the shared interface — an
// inline static no-argument `create()` factory, an `init()`, and a
// `get_utc_epoch(server)` that queries `server` for the current UTC time,
// returning Unix epoch seconds on success and `nullopt` if every attempt fails.
//
// `get_utc_epoch()` accepts optional retry-count / timeout arguments so callers
// can override the default policy; both implementations must accept them.
template<typename T>
concept NetworkTimeProtocolLike =
    std::is_class_v<T> && requires(T& ntp, const std::string& server) {
        { T::create() } -> std::same_as<T>;
        { std::declval<T&>().init() } -> std::same_as<esp_err_t>;
        {
            ntp.get_utc_epoch(server)
        } -> std::same_as<std::optional<std::uint32_t>>;
        {
            ntp.get_utc_epoch(server, kNtpDefaultRetryCount,
                              kNtpDefaultTimeoutMs)
        } -> std::same_as<std::optional<std::uint32_t>>;
    };

}  // namespace thermo::hal
