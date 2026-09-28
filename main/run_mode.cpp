// Run-mode entry point (§3.3 - §3.10).
//
// STATUS: STUBBED for this milestone. Configure mode is fully implemented;
// Run mode is scaffolded to mirror the §3.4 sampling loop so that the
// hardware paths can be filled in incrementally.
//
// What is real here:
//   - boot self-test LEDs (§7)
//   - battery read + hysteresis evaluation (§3.6)
//   - sensor read using cached ROM IDs, with a first-boot scan (§3.4)
//   - sample append to the RTC buffer (§4)
//   - deep sleep between samples (§3.8)
//
// What is intentionally stubbed:
//   - Wi-Fi station connect and cached fast-reconnect (§3.9)
//   - batch upload + payload formatting (§9)
//   - exponential backoff on failure (§3.7)
#include "run_mode.hpp"

#include <algorithm>
#include <string>
#include <vector>

#include "app_config.hpp"
#include "app_config_store.hpp"
#include "battery_monitor.hpp"
#include "constants.hpp"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led_manager.hpp"
#include "payload.hpp"
#include "rtc_state.hpp"
#include "sensor.hpp"

namespace thermo {
namespace {

constexpr const char* kTag = "run";

// Builds a fixed-size sample record from a reading set (§4.1).
SampleRecord make_record(std::uint32_t age_base, std::uint16_t batt_mv,
                         const std::vector<SensorReading>& readings) {
    SampleRecord rec{};
    rec.age_base = age_base;
    rec.batt_mv = batt_mv;
    rec.t1 = kTempNull;
    rec.t2 = kTempNull;
    rec.t3 = kTempNull;

    std::int16_t* const slots[limits::kSensorCount] = {&rec.t1, &rec.t2,
                                                       &rec.t3};
    for (std::size_t i = 0; i < readings.size() && i < limits::kSensorCount;
         ++i) {
        if (readings[i].temp_c_x100) {
            *slots[i] = *readings[i].temp_c_x100;
        }
    }
    return rec;
}

// Translates buffered records into the name-indexed form used by the payload
// builder (§9.1). Sensor order is fixed by the record layout (§4.1).
std::vector<payload::NamedSample> collect_batch(const RtcState& st) {
    std::vector<payload::NamedSample> out;
    out.reserve(st.buffer_len);
    for (std::size_t i = 0; i < st.buffer_len; ++i) {
        const SampleRecord& rec = st.buffer[i];
        payload::NamedSample sample{};
        sample.age_s = rec.age_base;
        sample.batt_mv = rec.batt_mv;
        sample.temps_c_x100 = {rec.t1, rec.t2, rec.t3};
        out.push_back(sample);
    }
    return out;
}

// Friendly names in column order, padded to the sensor count (§5.1.3).
std::vector<std::string> names_for(const Config& cfg,
                                   const std::vector<SensorReading>& readings) {
    std::vector<std::string> names;
    names.reserve(limits::kSensorCount);
    for (std::size_t i = 0; i < limits::kSensorCount && i < readings.size();
         ++i) {
        std::string name;
        for (const auto& mapping : cfg.mappings) {
            if (mapping.rom == readings[i].rom.to_hex()) {
                name = mapping.name;
                break;
            }
        }
        names.push_back(std::move(name));
    }
    return names;
}

// STUB: §3.5 batch upload. The payload is fully formatted (§9) and logged so
// the wire format can be reviewed, but no connection is attempted yet. Returns
// false to represent "not yet implemented", which triggers backoff (§3.7).
bool stub_upload(const Config& cfg, RtcState& st,
                 const std::vector<SensorReading>& readings) {
    const auto batch = collect_batch(st);
    const std::string json = payload::build_batch_json(
        cfg.dev_name, names_for(cfg, readings), batch);
    ESP_LOGW(kTag,
             "STUB: %u samples to '%s' not uploaded. Payload (%u bytes): %s",
             static_cast<unsigned>(st.buffer_len), cfg.dest_url.c_str(),
             static_cast<unsigned>(json.size()), json.c_str());
    return false;
}

// Enters deep sleep for `seconds` (§3.8).
void deep_sleep_for(std::uint32_t seconds) {
    ESP_LOGI(kTag, "deep sleep for %u s", static_cast<unsigned>(seconds));
    esp_sleep_enable_timer_wakeup(static_cast<std::uint64_t>(seconds) *
                                  1000000ULL);
    esp_deep_sleep_start();
}

}  // namespace

void run_mode_cycle() {
    RtcState& st = rtc_state();
    st.boot_count++;

    // §7 "Run - boot": both LEDs briefly on (self-test).
    LedManager leds;
    leds.init();
    leds.boot_flash();

    Config cfg;
    config_load(cfg);

    ESP_LOGI(kTag, "Run mode boot #%u (fail_count=%u, buffered=%u)",
             static_cast<unsigned>(st.boot_count),
             static_cast<unsigned>(st.fail_count),
             static_cast<unsigned>(st.buffer_len));

    // --- Battery + hysteresis (§3.6) ---
    BatteryMonitor battery;
    battery.init();
    std::uint32_t batt_mv = 0;
    if (auto mv = battery.read_mv(cfg)) {
        batt_mv = *mv;
    } else {
        ESP_LOGW(kTag, "battery read failed; assuming 0 mV");
    }

    auto tx_state = static_cast<TxState>(st.tx_state);
    const TxState previous = tx_state;
    tx_state = BatteryMonitor::evaluate_tx_state(batt_mv, cfg, tx_state);
    st.tx_state = static_cast<std::uint8_t>(tx_state);
    if (previous == TxState::kTxInhibited && tx_state == TxState::kTxOk) {
        // §7 "Run - recovered": green 1 s, then off.
        leds.recovered_pulse();
    }

    // --- Sample (§3.4) ---
    leds.set_sampling();

    SensorManager sensors(pins::kWireBus);
    sensors.init();

    // Prefer cached ROM IDs (§3.4: no bus scan on wake). On first boot the
    // cache is empty and SensorManager scans, then we retain the result.
    std::vector<RomId> cached;
    for (std::size_t i = 0; i < st.rom_count && i < limits::kSensorCount; ++i) {
        cached.push_back(st.rom_ids[i]);
    }
    std::vector<RomId> discovered;
    const auto readings = sensors.read_all(cached, &discovered);
    if (st.rom_count == 0 && !discovered.empty()) {
        st.rom_count = static_cast<std::uint8_t>(
            std::min<std::size_t>(discovered.size(), limits::kSensorCount));
        for (std::size_t i = 0; i < st.rom_count; ++i) {
            st.rom_ids[i] = discovered[i];
        }
        ESP_LOGI(kTag, "cached %u ROM IDs",
                 static_cast<unsigned>(st.rom_count));
    }

    const std::uint32_t age_s =
        static_cast<std::uint32_t>(esp_timer_get_time() / 1000000ULL);
    rtc_buffer_push(
        st, make_record(age_s, static_cast<std::uint16_t>(batt_mv), readings));

    // --- Upload decision (§3.4 step 4, §3.5) ---
    const bool tx_allowed = tx_state == TxState::kTxOk;
    const bool batch_ready = st.buffer_len >= cfg.batch_size;

    if (tx_allowed && batch_ready) {
        leds.set_uploading();
        if (stub_upload(cfg, st, readings)) {
            // Success path (unreachable until the uploader is implemented):
            // pulse green, clear the buffer, reset fail_count, update cache.
            leds.upload_ok_pulse();
            rtc_buffer_clear(st);
            st.fail_count = 0;
            st.last_upload_ok = 1;
        } else {
            // Failure path (§3.7): red on, increment backoff, clear Wi-Fi
            // cache.
            leds.set_tx_error();
            if (st.fail_count < 0xFF) {
                st.fail_count++;
            }
            st.last_upload_ok = 0;
            rtc_wifi_cache_clear(st);  // §3.10
        }
    } else if (!tx_allowed) {
        // §7 "Run - TX inhibited": red blink every 10 s. In deep sleep the LED
        // is off between wakes, so we simply signal briefly then sleep.
        leds.set_inhibited();
        leds.poll(static_cast<std::uint32_t>(esp_timer_get_time() / 1000));
        ESP_LOGI(kTag, "TX inhibited (batt %u mV); buffering sample",
                 static_cast<unsigned>(batt_mv));
    }

    // --- Determine sleep duration ---
    std::uint32_t sleep_s = cfg.interval_s;
    if (tx_allowed && batch_ready && st.fail_count > 0) {
        // §3.7 exponential backoff: min(base * 2^(fail_count-1), max).
        std::uint64_t backoff = static_cast<std::uint64_t>(cfg.retry_base_s)
                                << (st.fail_count - 1);
        if (backoff > cfg.retry_max_s) {
            backoff = cfg.retry_max_s;
        }
        sleep_s = static_cast<std::uint32_t>(backoff);
        ESP_LOGW(kTag, "upload failed; backoff %u s (fail_count=%u)",
                 static_cast<unsigned>(sleep_s),
                 static_cast<unsigned>(st.fail_count));
    }

    // §8: track absolute next sample time (drift avoidance).
    st.next_sample_at += sleep_s;

    deep_sleep_for(sleep_s);  // Does not return.
}

}  // namespace thermo
