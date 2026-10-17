// Configure-mode workflow (§3.2, §5, §6).
#include "configure_mode.hpp"

#include <string>
#include <utility>
#include <vector>

#include "app_config.hpp"
#include "app_config_store.hpp"
#include "config_form.hpp"
#include "config_page.hpp"
#include "constants.hpp"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hal/network_time_protocol.hpp"
#include "https_server.hpp"
#include "power_manager.hpp"
#include "sensor.hpp"
#include "wifi_ap.hpp"

namespace thermo {
namespace {

constexpr const char* kTag = "configure";

// Mutable state shared by the page handlers for the duration of Configure
// mode. `server` holds references to `cfg` and `sensors`, all of which outlive
// it because Configure mode never returns.
struct ConfigureContext {
    ConfigureContext(Config& cfg_in, SensorManager& sensors_in,
                     PowerManager& power_manager_in,
                     hal::NetworkTimeProtocol& ntp_in)
        : cfg(cfg_in),
          sensors(sensors_in),
          power_manager(power_manager_in),
          ntp(ntp_in) {}

    Config& cfg;
    SensorManager& sensors;
    PowerManager& power_manager;
    // The NTP client used for the save-time sync (§5.3). Owned by the caller
    // and outlives this context.
    hal::NetworkTimeProtocol& ntp;
    // Warning from the most recent save (e.g. NTP failure), shown on the
    // following GET /saved. Empty when there is nothing to report.
    std::string last_warning;
};

// Builds an HTML response with the default "200 OK" status.
HttpResponse html_response(std::string body) {
    HttpResponse response;
    response.body = std::move(body);
    return response;
}

// Builds a plain-text error response.
HttpResponse error_response(std::string status, std::string body) {
    HttpResponse response;
    response.status = std::move(status);
    response.content_type = "text/plain";
    response.body = std::move(body);
    return response;
}

// Builds a scan snapshot from live hardware for the / and /scan pages.
ScanSnapshot make_snapshot(ConfigureContext& ctx) {
    ScanSnapshot snap;
    snap.power_source = ctx.power_manager.source();
    snap.battery_mv = ctx.power_manager.read_mv();

    // Read every sensor whose ROM ID is known from the mapping table. When the
    // table is empty (fresh device), SensorManager performs a bus scan.
    std::vector<RomId> roms;
    for (const auto& m : ctx.cfg.mappings) {
        if (auto rom = RomId::from_hex(m.rom)) {
            roms.push_back(*rom);
        }
    }

    std::vector<RomId> discovered;
    snap.sensors = ctx.sensors.read_all(roms, &discovered);

    // Seed the mapping table with any newly discovered ROM IDs so they appear
    // in the config page (§5.1.3).
    if (roms.empty() && !discovered.empty()) {
        for (const auto& rom : discovered) {
            ctx.cfg.mappings.push_back({rom.to_hex(), rom.to_hex()});
        }
    }
    return snap;
}

// Runs the NTP sync for saved_time (§5.3 step 4-6) and records any warning.
void sync_saved_time(ConfigureContext& ctx) {
    ctx.last_warning.clear();
    const auto epoch = ctx.ntp.get_utc_epoch(
        ctx.cfg.ntp_server, timing::kNtpRetryCount, timing::kNtpTimeoutMs);
    if (!epoch) {
        ctx.last_warning =
            "NTP sync failed; 'Last Configuration Saved' was "
            "not updated. Other settings were saved.";
        return;
    }
    ctx.cfg.saved_time = *epoch;
    // Persist the updated saved_time alongside the rest of the configuration.
    config_save(ctx.cfg);
    ESP_LOGI(kTag, "saved_time updated to %u", static_cast<unsigned>(*epoch));
}

// GET / -- the configuration form (§6).
HttpResponse handle_root(ConfigureContext& ctx) {
    const ScanSnapshot scan = make_snapshot(ctx);
    return html_response(
        config_page::render_form(ctx.cfg, scan, /*errors=*/{}));
}

// GET /scan -- JSON snapshot (§6.1).
HttpResponse handle_scan(ConfigureContext& ctx) {
    const ScanSnapshot scan = make_snapshot(ctx);
    HttpResponse response;
    response.content_type = "application/json";
    response.body = config_page::render_scan_json(ctx.cfg, scan);
    return response;
}

// POST /save -- validate, persist, NTP sync, then redirect (§5.3).
HttpResponse handle_save(ConfigureContext& ctx, HttpRequest& request) {
    const auto body = request.body();
    if (!body) {
        return error_response("400 Bad Request", "Bad Request");
    }

    const auto fields = config_form::parse(*body);
    const Config candidate = config_form::to_config(ctx.cfg, fields);

    // Validate (§5.3 step 1). On failure, re-render with errors and do NOT
    // touch saved_time or NVS (§5.3 step 2).
    const ValidationResult result = validation::validate(candidate);
    if (!result.ok) {
        const ScanSnapshot scan = make_snapshot(ctx);
        return error_response(
            "400 Bad Request",
            config_page::render_form(candidate, scan, result.errors));
    }

    // Persist the validated configuration (§5.3 step 3).
    const esp_err_t err = config_save(candidate);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "config_save failed: %s", esp_err_to_name(err));
        return error_response("500 Internal Server Error",
                              "Failed to persist configuration");
    }
    ctx.cfg = candidate;

    // NTP sync for saved_time; any warning is shown on the next /saved.
    sync_saved_time(ctx);

    // Redirect to /saved (§6).
    HttpResponse response;
    response.status = "303 See Other";
    response.headers.emplace_back("Location", "/saved");
    return response;
}

// GET /saved -- confirmation page (§6).
HttpResponse handle_saved(ConfigureContext& ctx) {
    return html_response(config_page::render_saved(ctx.cfg, ctx.last_warning));
}

// Registers the four Configure-mode pages (§6) on `server`. Every handler
// captures `ctx`, which outlives the server.
esp_err_t register_pages(HttpsServer& server, ConfigureContext& ctx) {
    esp_err_t err = server.register_handler(
        "/", HttpMethod::kGet,
        [&ctx](HttpRequest&) { return handle_root(ctx); });
    if (err != ESP_OK) {
        return err;
    }
    err = server.register_handler(
        "/scan", HttpMethod::kGet,
        [&ctx](HttpRequest&) { return handle_scan(ctx); });
    if (err != ESP_OK) {
        return err;
    }
    err = server.register_handler(
        "/save", HttpMethod::kPost,
        [&ctx](HttpRequest& req) { return handle_save(ctx, req); });
    if (err != ESP_OK) {
        return err;
    }
    return server.register_handler(
        "/saved", HttpMethod::kGet,
        [&ctx](HttpRequest&) { return handle_saved(ctx); });
}

}  // namespace

[[noreturn]]
void run_configure_mode(LedTask& led_task) {
    ESP_LOGI(kTag, "entering Configure mode");

    Config cfg;
    config_load(cfg);

    // Bring up the SoftAP (§3.2) and the sensors used by the config page.
    WifiAp ap;
    esp_err_t err = ap.start(cfg);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "failed to start SoftAP: %s", esp_err_to_name(err));
        abort();
    }

    SensorManager sensors(pins::kWireBus);
    sensors.init();

    PowerManager power_manager(cfg);

    // NTP client for the save-time sync (§5.3). Constructed standalone rather
    // than through a full Board: Configure mode brings up the sensor/AP path
    // itself, so only the one HAL device it needs is instantiated here.
    hal::NetworkTimeProtocol ntp = hal::NetworkTimeProtocol::create();
    if (esp_err_t ntp_err = ntp.init(); ntp_err != ESP_OK) {
        ESP_LOGW(kTag, "NTP client init failed: %s", esp_err_to_name(ntp_err));
    }

    ConfigureContext ctx{cfg, sensors, power_manager, ntp};

    // The server is an empty router; the pages are registered here and their
    // handlers capture `ctx`, which outlives the server for the duration of
    // Configure mode.
    HttpsServer server;
    err = register_pages(server, ctx);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "failed to register pages: %s", esp_err_to_name(err));
        abort();
    }

    err = server.start();
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "failed to start HTTPS server: %s",
                 esp_err_to_name(err));
        abort();
    }

    ESP_LOGI(kTag, "config server ready at https://%s/",
             ap.ip_address().c_str());
    led_task.set_state(LedState::kConfig);

    // Stay in Configure mode. The switch is checked periodically; moving to
    // Run requires a power re-gate, so we simply idle here.
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

}  // namespace thermo
