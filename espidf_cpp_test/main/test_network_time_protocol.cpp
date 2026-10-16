// Native link check for the network time protocol (NTP) HAL (`hal_stub`).
//
// Proves the stub NTP client links and works under real FreeRTOS on the linux
// target: create/init and one `set_utc_epoch()` / `get_utc_epoch()` round-trip,
// plus the simulated failure path. The host suite in `test/` covers the stub in
// depth.
#include "unity.h"

#include "hal/network_time_protocol.hpp"
#include "hal/network_time_protocol_types.hpp"

namespace {
using thermo::hal::NetworkTimeProtocol;
}  // namespace

TEST_CASE("espidf_cpp_test ntp: create/init and round-trips a time",
          "[espidf_cpp_test][ntp]") {
    static_assert(thermo::hal::NetworkTimeProtocolLike<NetworkTimeProtocol>);

    NetworkTimeProtocol ntp = NetworkTimeProtocol::create();
    TEST_ASSERT_EQUAL(ESP_OK, ntp.init());

    // Has a plausible default time, then honours a set value.
    TEST_ASSERT_TRUE(ntp.get_utc_epoch("time.nrc.ca").has_value());

    ntp.set_utc_epoch(1700000042u);
    const auto epoch = ntp.get_utc_epoch("time.nrc.ca");
    TEST_ASSERT_TRUE(epoch.has_value());
    TEST_ASSERT_EQUAL_UINT32(1700000042u, *epoch);

    // The simulated-failure path links too.
    ntp.set_sync_fails(true);
    TEST_ASSERT_FALSE(ntp.get_utc_epoch("time.nrc.ca").has_value());
}
