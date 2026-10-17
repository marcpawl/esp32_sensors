// Tests for the stub network time protocol (NTP) client (`hal_stub`, §5.3,
// §5.4).
//
// The NTP HAL is deliberately minimal: the no-argument `create()` factory,
// `init()`, `get_utc_epoch(server)` (Unix epoch seconds, or `nullopt` on
// failure), and the stub-only controls `set_utc_epoch()` and
// `set_sync_fails()` used to simulate a served time and an unreachable server.
// These tests cover the *stub device* contract — construction, `init()`, the
// default reading, round-trips through `set_utc_epoch()`, the simulated failure
// path, and the shared `NetworkTimeProtocolLike` concept. They run on the
// ESP-IDF `linux` target with Unity and link the pin-free `hal_stub` component
// (never `hal_board`).
//
// The stub holds its reported time in software, so every case is deterministic
// and needs no network or server.
#include "unity.h"

#include <concepts>
#include <optional>
#include <string>

#include "hal/network_time_protocol.hpp"
#include "hal/network_time_protocol_types.hpp"

namespace {

// Convenience alias for the shared HAL vocabulary.
using thermo::hal::NetworkTimeProtocol;

constexpr std::string kServer = "time.nrc.ca";

NetworkTimeProtocol make_ntp() {
    NetworkTimeProtocol ntp = NetworkTimeProtocol::create();
    ntp.init();
    return ntp;
}

}  // namespace

// --- Construction / factory -------------------------------------------------

TEST_CASE("hal ntp: create() takes no arguments", "[hal_ntp]") {
    // The factory is parameterless for the stub and the board alike.
    static_assert(std::same_as<decltype(NetworkTimeProtocol::create()),
                               NetworkTimeProtocol>);
    NetworkTimeProtocol ntp = NetworkTimeProtocol::create();
    TEST_ASSERT_EQUAL(ESP_OK, ntp.init());
}

TEST_CASE("hal ntp: init() is idempotent and returns ESP_OK", "[hal_ntp]") {
    NetworkTimeProtocol ntp = make_ntp();
    TEST_ASSERT_EQUAL(ESP_OK, ntp.init());
    TEST_ASSERT_EQUAL(ESP_OK, ntp.init());
}

// --- Default reading --------------------------------------------------------

TEST_CASE("hal ntp: get_utc_epoch() defaults to a plausible fixed time",
          "[hal_ntp]") {
    NetworkTimeProtocol ntp = make_ntp();
    const auto epoch = ntp.get_utc_epoch(kServer);
    TEST_ASSERT_TRUE(epoch.has_value());
    // 2023-11-14 ~22:13 UTC; just past 2020 so it looks like a real time.
    TEST_ASSERT_EQUAL_UINT32(1700000000u, *epoch);
}

// --- Round-trips through set_utc_epoch() ------------------------------------

TEST_CASE("hal ntp: set_utc_epoch() is reflected by get_utc_epoch()",
          "[hal_ntp]") {
    NetworkTimeProtocol ntp = make_ntp();

    ntp.set_utc_epoch(1700000001u);
    TEST_ASSERT_EQUAL_UINT32(1700000001u, *ntp.get_utc_epoch(kServer));

    ntp.set_utc_epoch(1234567890u);
    TEST_ASSERT_EQUAL_UINT32(1234567890u, *ntp.get_utc_epoch(kServer));
}

TEST_CASE("hal ntp: repeated set_utc_epoch() keeps the last value",
          "[hal_ntp]") {
    NetworkTimeProtocol ntp = make_ntp();
    ntp.set_utc_epoch(1000u);
    ntp.set_utc_epoch(2000u);
    ntp.set_utc_epoch(3050u);  // last write wins
    TEST_ASSERT_EQUAL_UINT32(3050u, *ntp.get_utc_epoch(kServer));
}

TEST_CASE("hal ntp: set_utc_epoch(0) is a valid reading", "[hal_ntp]") {
    NetworkTimeProtocol ntp = make_ntp();
    ntp.set_utc_epoch(0u);
    TEST_ASSERT_EQUAL_UINT32(0u, *ntp.get_utc_epoch(kServer));
}

// --- Overriding the retry/timeout policy ------------------------------------

TEST_CASE("hal ntp: get_utc_epoch() accepts an explicit retry/timeout policy",
          "[hal_ntp]") {
    // The defaults follow §5.3; the firmware passes its own values. The stub
    // accepts and ignores them, but the overload must exist (part of the
    // concept) so the call sites compile against either implementation.
    NetworkTimeProtocol ntp = make_ntp();
    const auto epoch = ntp.get_utc_epoch(kServer, /*retry_count=*/1,
                                         /*timeout_ms=*/250);
    TEST_ASSERT_TRUE(epoch.has_value());
    TEST_ASSERT_EQUAL_UINT32(1700000000u, *epoch);
}

// --- Simulating an unreachable server ---------------------------------------

TEST_CASE("hal ntp: set_sync_fails(true) makes get_utc_epoch() return nullopt",
          "[hal_ntp]") {
    NetworkTimeProtocol ntp = make_ntp();
    ntp.set_sync_fails(true);
    TEST_ASSERT_FALSE(ntp.get_utc_epoch(kServer).has_value());
}

TEST_CASE("hal ntp: clearing set_sync_fails() restores the served time",
          "[hal_ntp]") {
    NetworkTimeProtocol ntp = make_ntp();
    ntp.set_utc_epoch(1700000000u);

    ntp.set_sync_fails(true);
    TEST_ASSERT_FALSE(ntp.get_utc_epoch(kServer).has_value());

    ntp.set_sync_fails(false);
    TEST_ASSERT_EQUAL_UINT32(1700000000u, *ntp.get_utc_epoch(kServer));
}

// --- The stub satisfies the shared concept ---------------------------------

TEST_CASE("hal ntp: stub NetworkTimeProtocol models the concept", "[hal_ntp]") {
    static_assert(thermo::hal::NetworkTimeProtocolLike<NetworkTimeProtocol>);
    TEST_ASSERT_EQUAL(ESP_OK, make_ntp().init());
}
