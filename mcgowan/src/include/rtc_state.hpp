// RTC-memory layout and helpers (§8). All state here survives deep sleep and
// backoff reboots, but is lost on power removal.
#pragma once

// NOLINTBEGIN
#pragma GCC diagnostic push  // NOLINT(ignored diagnostics)
#pragma GCC diagnostic pop
// NOLINTEND

#include <array>

#include <cstdint>

#include "logic/led_manager.hpp"  // TxState
#include "logic/limits.hpp"       // kSensorCount, kMaxBufferRecords
#include "sensor.hpp"

namespace thermo {

// One buffered sample record (§4.1). Fixed-size and trivially copyable so it
// can live in RTC memory. Layout: 4 + 2 + 2 + 2 + 2 = 12 bytes, no padding,
// which keeps ~450 records well within the ~8 KB RTC slow memory budget.
struct SampleRecord {
    std::uint32_t age_base = 0;   // Seconds since boot at sample time.
    std::uint16_t batt_mv = 0;    // Battery voltage (mV).
    std::int16_t t1 = kTempNull;  // Temperature x100, or kTempNull.
    std::int16_t t2 = kTempNull;
    std::int16_t t3 = kTempNull;
};

static_assert(sizeof(SampleRecord) == 12, "SampleRecord layout must be stable");
static_assert(alignof(SampleRecord) == 4,
              "SampleRecord must be 4-byte aligned");

// Cached Wi-Fi connection parameters (§3.9).
struct WifiCache {
    std::uint8_t bssid[6] = {};
    std::uint8_t channel = 0;
    std::uint32_t ip = 0;
    std::uint8_t valid = 0;
};

// The complete RTC-resident application state (§8).
struct RtcState {
    std::uint32_t boot_count = 0;
    std::uint8_t fail_count = 0;
    std::uint8_t tx_state = static_cast<std::uint8_t>(TxState::kTxOk);
    std::array<SampleRecord, limits::kMaxBufferRecords> buffer = {};
    std::uint16_t buffer_len = 0;
    std::uint32_t next_sample_at = 0;
    std::array<RomId, limits::kSensorCount> rom_ids = {};
    std::uint8_t rom_count = 0;
    WifiCache wifi = {};
    std::uint8_t last_upload_ok = 0;
};

// Obtains the single RTC-resident state.
RtcState& rtc_state();

// Prepares the RTC state for this boot. Must be called once, early in app_main,
// before any other rtc_* function. On a cold boot (power-on or a reset that is
// not a deep-sleep wake) the block is zeroed and its counters reset; on a
// deep-sleep wake the retained contents are preserved.
void rtc_state_init();

// True when the current boot followed a deep-sleep wake.
bool rtc_woke_from_deep_sleep();

// Appends a sample, evicting the oldest on overflow (§4.2 FIFO).
void rtc_buffer_push(RtcState& st, const SampleRecord& rec);

// Clears the buffer after a successful upload.
void rtc_buffer_clear(RtcState& st);

// §3.10: invalidate the cached Wi-Fi parameters.
void rtc_wifi_cache_clear(RtcState& st);

}  // namespace thermo
