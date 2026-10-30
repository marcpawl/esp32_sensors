// RTC sample-buffer operations (§4).
//
// Part of the `logic` component, so the FIFO semantics run in the firmware and
// in the host tests from one definition. The storage (the `RTC_DATA_ATTR`
// block) is deliberately not here: it belongs to the firmware, in
// `rtc_storage.cpp`.
#include "logic/rtc_state.hpp"
// NOLINTBEGIN
#pragma GCC diagnostic push  // NOLINT(ignored diagnostics)
#pragma GCC diagnostic pop
// NOLINTEND

#include <cstddef>

namespace thermo {

void rtc_state_reset(RtcState& st) {
    st = RtcState();
    // `tx_state`'s default already repairs this, but say it plainly: a cold
    // boot always transmits, so a retained inhibition cannot survive a power
    // cycle.
    st.tx_state = static_cast<std::uint8_t>(TxState::kTxOk);
}

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
