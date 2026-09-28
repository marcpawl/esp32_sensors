// Battery voltage measurement and low/high-water hysteresis (§2.5, §3.6).
#include "battery_monitor.hpp"

#include <algorithm>

#include "constants.hpp"
#include "driver/gpio.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace thermo {
namespace {

constexpr const char* kTag = "battery";

// 12 dB attenuation covers up to ~3.1 V on the ADC pin; the divider must keep
// the input below that at maximum battery voltage (§2.4 GPIO 34 note).
constexpr adc_atten_t kAtten = ADC_ATTEN_DB_12;
constexpr adc_bitwidth_t kBitWidth = ADC_BITWIDTH_DEFAULT;
constexpr int kSampleCount = 16;

adc_oneshot_unit_handle_t g_adc = nullptr;
adc_cali_handle_t g_cali = nullptr;
bool g_cali_enabled = false;

void ensure_gate_gpio() {
    gpio_config_t cfg = {};
    cfg.pin_bit_mask = 1ULL << pins::kDividerGate;
    cfg.mode = GPIO_MODE_OUTPUT;
    cfg.pull_up_en = GPIO_PULLUP_DISABLE;
    cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    cfg.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&cfg);
}

void gate_enable(bool on) {
    // P-MOSFET gate: driving the gate low energizes the divider; high
    // disconnects it (§2.5).
    gpio_set_level(static_cast<gpio_num_t>(pins::kDividerGate), on ? 0 : 1);
}

// Reads the median-ish value: average after discarding the extremes.
std::optional<int> read_filtered_mv() {
    int samples[kSampleCount];
    for (int i = 0; i < kSampleCount; ++i) {
        int raw = 0;
        if (adc_oneshot_read(g_adc, ADC_CHANNEL_6 /* GPIO34 */, &raw) !=
            ESP_OK) {
            return std::nullopt;
        }
        samples[i] = raw;
    }
    std::sort(samples, samples + kSampleCount);
    // Discard the 4 highest and 4 lowest, average the rest.
    long sum = 0;
    int count = 0;
    for (int i = 4; i < kSampleCount - 4; ++i) {
        sum += samples[i];
        ++count;
    }
    const int avg_raw = static_cast<int>(sum / (count > 0 ? count : 1));

    if (!g_cali_enabled) {
        return std::nullopt;
    }
    int mv = 0;
    if (adc_cali_raw_to_voltage(g_cali, avg_raw, &mv) != ESP_OK) {
        return std::nullopt;
    }
    return mv;
}

}  // namespace

esp_err_t BatteryMonitor::init() {
    adc_oneshot_unit_init_cfg_t unit_cfg = {};
    unit_cfg.unit_id = ADC_UNIT_1;
    unit_cfg.clk_src = ADC_RTC_CLK_SRC_DEFAULT;
    unit_cfg.ulp_mode = ADC_ULP_MODE_DISABLE;
    esp_err_t err = adc_oneshot_new_unit(&unit_cfg, &g_adc);
    if (err != ESP_OK) {
        return err;
    }

    adc_oneshot_chan_cfg_t chan_cfg = {};
    chan_cfg.atten = kAtten;
    chan_cfg.bitwidth = kBitWidth;
    err = adc_oneshot_config_channel(g_adc, ADC_CHANNEL_6, &chan_cfg);
    if (err != ESP_OK) {
        return err;
    }

    // Classic ESP32 supports line-fitting calibration (curve fitting is only
    // available on newer SoCs). Line fitting on ESP32 additionally requires a
    // reference voltage; if the scheme cannot be created we fall back to an
    // approximate conversion in read_filtered_mv().
    adc_cali_line_fitting_config_t cali_cfg = {};
    cali_cfg.unit_id = ADC_UNIT_1;
    cali_cfg.atten = kAtten;
    cali_cfg.bitwidth = kBitWidth;
    cali_cfg.default_vref = 1100;  // Typical ESP32 eFuse Vref (mV).
    g_cali_enabled =
        (adc_cali_create_scheme_line_fitting(&cali_cfg, &g_cali) == ESP_OK);

    ensure_gate_gpio();
    initialized_ = true;
    return ESP_OK;
}

std::optional<std::uint32_t> BatteryMonitor::read_mv(const Config& cfg) {
    if (!initialized_) {
        return std::nullopt;
    }

    const bool gated = cfg.batt_gate_en;
    if (gated) {
        gate_enable(true);
        // Allow the divider to settle before sampling.
        vTaskDelay(pdMS_TO_TICKS(5));
    }

    const auto adc_mv = read_filtered_mv();

    if (gated) {
        gate_enable(false);
    }

    if (!adc_mv) {
        return std::nullopt;
    }

    // Undo the divider: V_batt = V_adc * (R_top + R_bottom) / R_bottom (§2.5).
    const double ratio = static_cast<double>(cfg.batt_r_top + cfg.batt_r_bot) /
                         static_cast<double>(cfg.batt_r_bot);
    double batt = static_cast<double>(*adc_mv) * ratio;
    // Apply calibration gain and offset.
    batt = batt * cfg.batt_cal_gain + cfg.batt_cal_offset;
    if (batt < 0) {
        batt = 0;
    }
    return static_cast<std::uint32_t>(batt);
}

TxState BatteryMonitor::evaluate_tx_state(std::uint32_t batt_mv,
                                          const Config& cfg, TxState current) {
    // §3.6 hysteresis:
    //  - On boot the caller passes kTxInhibited when batt < high water, which
    //    this function then re-evaluates.
    //  - TX_OK drops to inhibited when batt < low water.
    //  - TX_INHIBITED returns to OK when batt > high water.
    switch (current) {
        case TxState::kTxOk:
            if (batt_mv < cfg.batt_low_mv) {
                return TxState::kTxInhibited;
            }
            return TxState::kTxOk;
        case TxState::kTxInhibited:
            if (batt_mv > cfg.batt_high_mv) {
                return TxState::kTxOk;
            }
            return TxState::kTxInhibited;
    }
    return current;
}

}  // namespace thermo
