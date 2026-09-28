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

void rtc_buffer_push(RtcState& st, const SampleRecord& rec) {
    if (st.buffer_len < limits::kMaxBufferRecords) {
        st.buffer[st.buffer_len++] = rec;
        return;
    }
    // Overflow: drop the oldest (§4.2, FIFO eviction).
    std::memmove(&st.buffer[0], &st.buffer[1],
                 sizeof(SampleRecord) * (limits::kMaxBufferRecords - 1));
    st.buffer[limits::kMaxBufferRecords - 1] = rec;
}

void rtc_buffer_clear(RtcState& st) { st.buffer_len = 0; }

void rtc_wifi_cache_clear(RtcState& st) {
    std::memset(&st.wifi, 0, sizeof(st.wifi));
    st.wifi.valid = 0;
}

} // namespace thermo
