// RTC-memory state and buffer management (§4, §8).
#include "rtc_state.hpp"

#include <cstdint>
#include <cstring>

#include "esp_attr.h"
#include "esp_sleep.h"

namespace thermo {
namespace {

// The single RTC-resident state block. RTC_DATA_ATTR places it in RTC slow
// memory, which is retained across deep sleep.
RTC_DATA_ATTR RtcState g_rtc_state;

// Marks whether the RTC block has been initialized since power-on.
RTC_DATA_ATTR std::uint8_t g_rtc_initialized;

} // namespace

RtcState& rtc_state() { return g_rtc_state; }

void rtc_state_init() {
    if (rtc_woke_from_deep_sleep()) {
        // State was retained across sleep; nothing to reset this boot.
        return;
    }
    // Cold boot: clear everything deterministic. The buffer and caches are
    // intentionally lost on power removal (§4.3).
    std::memset(&g_rtc_state, 0, sizeof(g_rtc_state));
    g_rtc_state.tx_state = static_cast<std::uint8_t>(TxState::kTxOk);
    g_rtc_initialized = 1;
}

bool rtc_woke_from_deep_sleep() {
    const std::uint32_t causes = esp_sleep_get_wakeup_causes();
    return (causes & (1u << ESP_SLEEP_WAKEUP_TIMER)) != 0;
}

// NOTE: rtc_buffer_push / rtc_buffer_clear / rtc_wifi_cache_clear live in
// rtc_buffer.cpp so they can be unit-tested on the host target without pulling
// in esp_sleep.h (unavailable for the ESP-IDF `linux` target).

} // namespace thermo
