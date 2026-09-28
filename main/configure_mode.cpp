// Configure-mode workflow (§3.2, §5, §6).
#include "configure_mode.hpp"

#include <string>
#include <vector>

#include "app_config.hpp"
#include "app_config_store.hpp"
#include "battery_monitor.hpp"
#include "config_page.hpp"
#include "constants.hpp"
#include "esp_log.h"
#include "https_server.hpp"
#include "led_manager.hpp"
#include "ntp_client.hpp"
#include "sensor.hpp"
#include "wifi_ap.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace thermo {
namespace {

constexpr const char* kTag = "configure";

// Builds a scan snapshot from live hardware for the /scan and / form routes.
ScanSnapshot make_snapshot(Config& cfg, SensorManager& sensors,
                           BatteryMonitor& battery) {
    ScanSnapshot snap;

    if (auto mv = battery.read_mv(cfg)) {
        snap.battery_mv = *mv;
    }

    // Read every sensor whose ROM ID is known from the mapping table. When the
    // table is empty (fresh device), SensorManager performs a bus scan.
    std::vector<RomId> roms;
    for (const auto& m : cfg.mappings) {
        if (auto rom = RomId::from_hex(m.rom)) {
            roms.push_back(*rom);
        }
    }

    std::vector<RomId> discovered;
    snap.sensors = sensors.read_all(roms, &discovered);

    // Seed the mapping table with any newly discovered ROM IDs so they appear
    // in the config page (§5.1.3).
    if (roms.empty() && !discovered.empty()) {
        for (const auto& rom : discovered) {
            cfg.mappings.push_back({rom.to_hex(), rom.to_hex()});
        }
    }
    return snap;
}

// Handles POST /save: NTP sync for saved_time (§5.3 step 4-6).
std::optional<std::string> on_save(Config& cfg) {
    // Only the NTP sync happens here; validation and persistence are done by
    // the HTTPS server before this callback runs.
    const auto epoch = ntp_get_utc_epoch(cfg.ntp_server);
    if (!epoch) {
        return std::string("NTP sync failed; 'Last Configuration Saved' was "
                           "not updated. Other settings were saved.");
    }
    cfg.saved_time = *epoch;
    // Persist the updated saved_time alongside the rest of the configuration.
    config_save(cfg);
    ESP_LOGI(kTag, "saved_time updated to %u", static_cast<unsigned>(*epoch));
    return std::nullopt;
}

} // namespace

void run_configure_mode() {
    ESP_LOGI(kTag, "entering Configure mode");

    // §7: both LEDs solid ON.
    LedManager leds;
    leds.init();
    leds.set_configure_mode();

    Config cfg;
    config_load(cfg);

    // Bring up the SoftAP (§3.2) and the sensors used by the config page.
    WifiAp ap;
    esp_err_t err = ap.start(cfg);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "failed to start SoftAP: %s", esp_err_to_name(err));
        leds.set_fatal();
        return;
    }

    SensorManager sensors(pins::kWireBus);
    sensors.init();

    BatteryMonitor battery;
    battery.init();

    // The server holds references to `cfg`, `sensors`, and `battery`, all of
    // which outlive it for the duration of Configure mode.
    HttpsServer server(
        cfg,
        [&cfg, &sensors, &battery]() {
            return make_snapshot(cfg, sensors, battery);
        },
        [&cfg](const Config&) { return on_save(cfg); });

    err = server.start();
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "failed to start HTTPS server: %s",
                 esp_err_to_name(err));
        leds.set_fatal();
        return;
    }

    ESP_LOGI(kTag, "config server ready at https://%s/",
             ap.ip_address().c_str());

    // Stay in Configure mode. The switch is checked periodically; moving to
    // Run requires a power re-gate, so we simply idle here.
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

} // namespace thermo
