// Shared, implementation-free types for the WiFi radio (§3.2, §3.9, §5.
// 1.1).
//
// This header is the *single* definition of the WiFi vocabulary — the
// `AccessPointConfig` and `StationConfig` descriptions and the
// `WifiRadioLike` concept — used by both WiFi implementations:
//
//   - `hal_board` — real ESP-IDF WiFi (SoftAP + station) implementation.
//   - `hal_stub`  — software-driven stub used by the host tests.
//
// Each implementation provides its own `hal/wifi.hpp` that includes this
// header and adds the concrete `Wifi` class, so there is exactly one
// definition of the shared contract and the two implementations cannot drift.
// (A project still links exactly one of `hal_board` / `hal_stub`, but both
// `REQUIRES hal_common`.)
//
// The HAL is *semantics-free*: it knows how to bring an access point or a
// station up and down, but not when to. Configure mode (§3.2) may run both at
// once; the caller decides, and the two are independent — stopping one never
// stops the other.
//
// The credentials are plain text fixed-size copies, so the types are trivially
// copyable and hold no ownership; `copy_*()` helpers store and truncate
// (asserting rather than silently losing) a longer string.
#pragma once

#include <algorithm>
#include <array>
#include <cassert>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

#include "esp_err.h"

namespace thermo::hal {

// Length limits from IEEE 802.11 (SSID ≤ 32 octets, PSK passphrase 8–63
// octets). The buffers carry one extra byte for the terminating NUL.
inline constexpr std::size_t kMaxSsidLength = 32;
inline constexpr std::size_t kMinPasswordLength = 8;
inline constexpr std::size_t kMaxPasswordLength = 63;

// Copies `text` (up to `Capacity - 1` bytes plus the NUL) into `buffer`.
// Text that does not fit is a programming error at this layer (validation
// happens before the HAL is called), so it trips an assert instead of
// silently dropping characters; the copy stays in bounds either way. Returns
// the stored length. `text` may be null, which stores the empty string.
template<std::size_t Capacity>
constexpr std::size_t copy_text(std::array<char, Capacity>& buffer,
                                const char* text) noexcept {
    static_assert(Capacity >= 1);
    buffer.fill('\0');
    if (text == nullptr) {
        return 0;
    }
    std::size_t const length = std::min(std::strlen(text), Capacity - 1);
    assert(length < Capacity);
    std::memcpy(buffer.data(), text, length);
    return length;
}

// The configuration for the SoftAP (§3.2, §5.1.1). `max_connections` is the
// number of clients the AP may serve at once.
struct AccessPointConfig {
    std::array<char, kMaxSsidLength + 1> ssid{};
    std::array<char, kMaxPasswordLength + 1> password{};
    std::uint8_t channel = 1;
    std::uint8_t max_connections = 4;
    bool hidden = false;

    friend constexpr bool operator==(AccessPointConfig const&,
                                     AccessPointConfig const&) = default;
};

// The configuration for the station (§3.9, §5.1.1). An empty `ssid` means "no
// station configured", which the caller should treat as "do not start the
// station" (§3.2).
struct StationConfig {
    std::array<char, kMaxSsidLength + 1> ssid{};
    std::array<char, kMaxPasswordLength + 1> password{};

    [[nodiscard]] constexpr bool has_ssid() const noexcept {
        return ssid[0] != '\0';
    }

    friend constexpr bool operator==(StationConfig const&,
                                     StationConfig const&) = default;
};

// Concept: a WiFi type that exposes the shared interface — an inline static
// `create()` factory taking no parameters, an `init()` that must be called
// before anything else, and an independent start/stop pair for the access
// point and for the station.
//
// `start_ap()` / `start_station()` configure the interface from the given
// struct and bring it up (restarting it if it is already running);
// `stop_ap()` / `stop_station()` take it down. Both implementations satisfy
// the concept, so a project can `static_assert` which one it linked in.
template<typename T>
concept WifiRadioLike =
    std::is_class_v<T> &&
    requires(T& wifi, AccessPointConfig const& ap, StationConfig const& sta) {
        { T::create() } -> std::same_as<T>;
        { wifi.init() } -> std::same_as<esp_err_t>;
        { wifi.start_ap(ap) } -> std::same_as<esp_err_t>;
        { wifi.stop_ap() } -> std::same_as<esp_err_t>;
        { wifi.start_station(sta) } -> std::same_as<esp_err_t>;
        { wifi.stop_station() } -> std::same_as<esp_err_t>;
    };

}  // namespace thermo::hal
