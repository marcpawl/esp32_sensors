// RTC sample-buffer operations (§4).
//
// Separated from rtc_state.cpp so the FIFO semantics can be unit-tested on the
// host target: rtc_state.cpp pulls in esp_sleep.h (deep-sleep wake detection),
// which is unavailable for the ESP-IDF `linux` target, whereas the buffer
// operations below are pure and dependency-free.
#include "rtc_state.hpp"

#include <cstring>

namespace thermo {

void rtc_buffer_push(RtcState& st, const SampleRecord& rec) {
    if (st.buffer_len < limits::kMaxBufferRecords) {
        st.buffer[st.buffer_len++] = rec;
        return;
    }
    // Overflow: drop the oldest (§4.2, FIFO eviction) and append the newest.
    std::memmove(&st.buffer[0], &st.buffer[1],
                 sizeof(SampleRecord) * (limits::kMaxBufferRecords - 1));
    st.buffer[limits::kMaxBufferRecords - 1] = rec;
}

void rtc_buffer_clear(RtcState& st) {
    st.buffer_len = 0;
}

void rtc_wifi_cache_clear(RtcState& st) {
    std::memset(&st.wifi, 0, sizeof(st.wifi));
    st.wifi.valid = 0;
}

}  // namespace thermo
