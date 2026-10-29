// RTC sample-buffer operations (§4).
//
// Separated from rtc_state.cpp so the FIFO semantics can be unit-tested on the
// host target: rtc_state.cpp pulls in esp_sleep.h (deep-sleep wake detection),
// which is unavailable for the ESP-IDF `linux` target, whereas the buffer
// operations below are pure and dependency-free.
#include "rtc_state.hpp"
// NOLINTBEGIN
#pragma GCC diagnostic push  // NOLINT(ignored diagnostics)
#pragma GCC diagnostic pop
// NOLINTEND

#include <cstddef>

namespace thermo {

void rtc_buffer_push(RtcState& st, const SampleRecord& rec) {
    if (!st.buffer.full()) {
        st.buffer.push_back(rec);
        return;
    }
    // Overflow: drop the oldest (§4.2, FIFO eviction) and append the newest.
    // SampleRecord is trivially copyable, so shifting one slot down is a plain
    // element-wise copy; the vector's length stays at capacity.
    for (std::size_t i = 1; i < st.buffer.size(); ++i) {
        st.buffer[i - 1] = st.buffer[i];
    }
    st.buffer.back() = rec;
}

void rtc_buffer_clear(RtcState& st) {
    st.buffer.clear();
}

void rtc_wifi_cache_clear(RtcState& st) {
    // Value-initialize rather than memset: WifiCache is a class type, so
    // clearing it with memset trips -Wclass-memaccess and bypasses the type's
    // own initialization.
    st.wifi = WifiCache{};
}

}  // namespace thermo
