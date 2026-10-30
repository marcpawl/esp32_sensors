// RTC-memory layout and the sample-buffer operations (§4, §8).
//
// Part of the `logic` component: the *shape* of the retained state and the
// buffer semantics are board-independent, so the firmware and the host tests
// share one definition. What is *not* here is the storage itself — placing a
// block in RTC slow memory (`RTC_DATA_ATTR`) and asking the hardware why the
// last wake happened are firmware concerns, and live in the firmware's
// `rtc_storage.hpp` / `rtc_storage.cpp`.
//
// Keeping the split this way is what makes the buffer host-testable: nothing
// in this header touches ESP-IDF (no `esp_attr.h`, no `esp_sleep.h`), so it
// builds for the `linux` target, where `RTC_DATA_ATTR` expands to a
// compile-time failure and the sleep API is unlinkable.
//
// The state here survives deep sleep and backoff reboots, but is lost on power
// removal (§4.3).
#pragma once

// NOLINTBEGIN
#pragma GCC diagnostic push  // NOLINT(ignored diagnostics)
#pragma GCC diagnostic pop
// NOLINTEND

#include <cstdint>
#include <type_traits>

#include "espidf_cpp/static_vector.hpp"  // static_vector
#include "hal/ds18b20_types.hpp"         // RomId
#include "logic/led_manager.hpp"         // TxState
#include "logic/limits.hpp"              // kSensorCount, kMaxBufferRecords
#include "logic/temp_format.hpp"         // kTempNull

namespace thermo {

// `RomId` is defined in the shared, implementation-free `hal_common` component;
// re-export it into `thermo` as `sensor.hpp` does, so the RTC state names it
// without pulling in the 1-Wire driver header.
using thermo::hal::RomId;

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
//
// The buffer and the cached ROM IDs are `static_vector`s: their length travels
// with the data instead of living in separate `buffer_len` / `rom_count`
// counters, so the two can never disagree (a mismatched counter would make the
// device read or upload stale RTC bytes). Storage stays inline, so the whole
// block is still a plain, trivially copyable POD suitable for `RTC_DATA_ATTR`.
struct RtcState {
    std::uint32_t boot_count = 0;
    std::uint8_t fail_count = 0;
    std::uint8_t tx_state = static_cast<std::uint8_t>(TxState::kTxOk);
    espidf_cpp::static_vector<SampleRecord, limits::kMaxBufferRecords> buffer{};
    std::uint32_t next_sample_at = 0;
    espidf_cpp::static_vector<RomId, limits::kSensorCount> rom_ids{};
    WifiCache wifi = {};
    std::uint8_t last_upload_ok = 0;
};

// RTC memory is restored verbatim from the previous boot, so the block is
// copied with a byte blit (and lives in a hardware-retained section): it must
// stay trivially copyable, and it must not own any out-of-line storage.
static_assert(std::is_trivially_copyable_v<RtcState>,
              "RtcState must be trivially copyable to live in RTC memory");

// Prepares `st` for a cold boot: clears everything deterministic. The buffer
// and the caches are intentionally lost on power removal (§4.3).
//
// The caller decides *whether* this boot is cold (on the firmware that is
// `rtc_state_init()` in `rtc_storage.hpp`, which asks the board for the wake
// cause), so this stays a pure, host-testable function.
void rtc_state_reset(RtcState& st);

// Appends a sample, evicting the oldest on overflow (§4.2 FIFO).
void rtc_buffer_push(RtcState& st, const SampleRecord& rec);

// Clears the buffer after a successful upload.
void rtc_buffer_clear(RtcState& st);

// §3.10: invalidate the cached Wi-Fi parameters.
void rtc_wifi_cache_clear(RtcState& st);

}  // namespace thermo
