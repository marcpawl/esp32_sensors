// HTML rendering for the configuration UI (§5.1, §6). Pure string building.
#include "config_page.hpp"

#include <cstdio>
#include <ctime>

#include "constants.hpp"

namespace thermo::config_page {
namespace {

// Appends a text input row to the form.
void field(std::string& out, const char* name, const char* label,
           const std::string& value, const char* type = "text",
           const char* extra = "") {
    out += "<p><label>" + html_escape(label) + "<br>";
    out += "<input type='" + std::string(type) + "' name='" + name +
           "' value='" + html_escape(value) + "' " + extra + "></label></p>\n";
}

void field_num(std::string& out, const char* name, const char* label,
               const std::string& value, const char* step = "1") {
    out += "<p><label>" + html_escape(label) + "<br>";
    out += "<input type='number' step='" + std::string(step) + "' name='" +
           name + "' value='" + html_escape(value) + "'></label></p>\n";
}

void checkbox(std::string& out, const char* name, const char* label, bool on) {
    out += "<p><label><input type='checkbox' name='" + std::string(name) +
           "' value='1' " + (on ? "checked" : "") + "> " + html_escape(label) +
           "</label></p>\n";
}

std::string to_string_u32(std::uint32_t v) {
    return std::to_string(v);
}

std::string to_string_f(float v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%g", static_cast<double>(v));
    return buf;
}

}  // namespace

std::string html_escape(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    for (char c : in) {
        switch (c) {
            case '&':
                out += "&amp;";
                break;
            case '<':
                out += "&lt;";
                break;
            case '>':
                out += "&gt;";
                break;
            case '"':
                out += "&quot;";
                break;
            case '\'':
                out += "&#39;";
                break;
            default:
                out.push_back(c);
        }
    }
    return out;
}

std::string format_saved_time(std::uint32_t epoch_utc) {
    if (epoch_utc == 0) {
        return "never";  // §6.2 initial state.
    }
    const std::time_t t = static_cast<std::time_t>(epoch_utc);
    std::tm tm_utc{};
    gmtime_r(&t, &tm_utc);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S UTC", &tm_utc);
    return buf;
}

std::string render_scan_json(const Config& cfg, const ScanSnapshot& scan) {
    // §6.1 response: battery_mv, unit, and a sensors array of
    // {rom, name, temp_c}. Field order is stable for easy diffing.
    std::string out = "{\n";
    out += "  \"battery_mv\": " + std::to_string(scan.battery_mv) + ",\n";
    out += "  \"unit\": \"C\",\n";
    out += "  \"sensors\": [\n";

    for (std::size_t i = 0; i < scan.sensors.size(); ++i) {
        const auto& reading = scan.sensors[i];
        const std::string rom = reading.rom.to_hex();

        // Resolve the friendly name from the mapping, defaulting to the ROM.
        std::string name = rom;
        for (const auto& m : cfg.mappings) {
            if (m.rom == rom) {
                name = m.name;
                break;
            }
        }

        out += "    { \"rom\": \"" + rom + "\", \"name\": \"" +
               html_escape(name) + "\", \"temp_c\": ";
        if (reading.temp_c_x100) {
            out += format_temp_c_x100(*reading.temp_c_x100);
        } else {
            out += "null";
        }
        out += " }";
        if (i + 1 < scan.sensors.size()) {
            out += ",";
        }
        out += "\n";
    }
    out += "  ]\n}\n";
    return out;
}

std::string render_form(const Config& cfg, const ScanSnapshot& scan,
                        const std::vector<ValidationError>& errors) {
    std::string out;
    out += "<!DOCTYPE html><html><head><meta charset='utf-8'>";
    out +=
        "<meta name='viewport' content='width=device-width,initial-scale=1'>";
    out += "<title>ESP32 Thermometer Configuration</title>";
    out +=
        "<style>body{font-family:sans-serif;margin:1rem;max-width:40rem}"
        "fieldset{margin-bottom:1rem}label{display:block}"
        "table{border-collapse:collapse;width:100%}"
        "td,th{border:1px solid #ccc;padding:4px;text-align:left}"
        ".err{color:#b00;font-size:0.9em}</style></head><body>";
    out += "<h1>ESP32 Thermometer Configuration</h1>";

    // Surface per-field validation errors after a failed save (§5.3 step 2).
    if (!errors.empty()) {
        out +=
            "<div class='err'><strong>Please correct the "
            "following:</strong><ul>";
        for (const auto& e : errors) {
            out += "<li>" + html_escape(e.field) + ": " +
                   html_escape(e.message) + "</li>";
        }
        out += "</ul></div>";
    }

    out += "<form method='POST' action='/save'>";

    // --- Network & Upload (§5.1.1) ---
    out += "<fieldset><legend>Network &amp; Upload</legend>";
    field(out, "ap_ssid", "AP SSID", cfg.ap_ssid);
    field(out, "ap_pass", "AP Password (8-63 chars)", cfg.ap_pass, "password");
    field(out, "sta_ssid", "Station SSID", cfg.sta_ssid);
    field(out, "sta_pass", "Station Password", cfg.sta_pass, "password");
    field(out, "dest_url", "Data Destination URL", cfg.dest_url);
    field_num(out, "interval_s", "Data Collection Interval (s)",
              to_string_u32(cfg.interval_s));
    field_num(out, "retry_base_s", "Error Retry Base (s)",
              to_string_u32(cfg.retry_base_s));
    field_num(out, "retry_max_s", "Error Retry Max (s)",
              to_string_u32(cfg.retry_max_s));
    field_num(out, "batch_size", "Batch Size N", to_string_u32(cfg.batch_size));
    field(out, "dev_name", "Device Name", cfg.dev_name);
    field(out, "ntp_server", "NTP Server", cfg.ntp_server);
    out += "<p><label>Last Configuration Saved<br>";
    out += "<input type='text' value='" +
           html_escape(format_saved_time(cfg.saved_time)) +
           "' readonly></label></p>";
    out += "</fieldset>";

    // --- Battery (§5.1.2) ---
    out += "<fieldset><legend>Battery - Thresholds &amp; Calibration</legend>";
    field_num(out, "batt_low_mv", "Low Water (mV)",
              to_string_u32(cfg.batt_low_mv));
    field_num(out, "batt_high_mv", "High Water (mV)",
              to_string_u32(cfg.batt_high_mv));
    field_num(out, "batt_r_top", "Divider R_top (ohm)",
              to_string_u32(cfg.batt_r_top));
    field_num(out, "batt_r_bot", "Divider R_bottom (ohm)",
              to_string_u32(cfg.batt_r_bot));
    field_num(out, "batt_cal_gain", "ADC Calibration Gain",
              to_string_f(cfg.batt_cal_gain), "0.0001");
    field_num(out, "batt_cal_offset", "ADC Calibration Offset (mV)",
              std::to_string(cfg.batt_cal_offset));
    checkbox(out, "batt_gate_en", "Divider Gating Enabled", cfg.batt_gate_en);
    out += "</fieldset>";

    // --- Device ID → Name Mapping (§5.1.3) ---
    out += "<fieldset><legend>Device ID &rarr; Name Mapping</legend>";
    out +=
        "<table><tr><th>ROM ID</th><th>Friendly Name</th>"
        "<th>Current Temp</th></tr>";
    for (const auto& reading : scan.sensors) {
        const std::string rom = reading.rom.to_hex();
        std::string name = rom;
        for (const auto& m : cfg.mappings) {
            if (m.rom == rom) {
                name = m.name;
                break;
            }
        }
        out += "<tr><td>" + rom + "</td>";
        out += "<td><input type='text' name='map_" + rom + "' value='" +
               html_escape(name) + "'></td>";
        out += "<td>";
        if (reading.temp_c_x100) {
            out += format_temp_c_x100(*reading.temp_c_x100) + " &deg;C";
        } else {
            out += "n/a";
        }
        out += "</td></tr>";
    }
    out += "</table></fieldset>";

    out += "<p><button type='submit'>Save</button></p>";
    out += "</form></body></html>";
    return out;
}

std::string render_saved(const Config& cfg, const std::string& warning) {
    std::string out;
    out += "<!DOCTYPE html><html><head><meta charset='utf-8'>";
    out += "<title>Configuration Saved</title></head><body>";
    out += "<h1>Configuration Saved</h1>";
    if (!warning.empty()) {
        out += "<p class='err'><strong>Warning:</strong> " +
               html_escape(warning) + "</p>";
    }
    out += "<p>Last Configuration Saved: " +
           html_escape(format_saved_time(cfg.saved_time)) + "</p>";
    out += "<p><a href='/'>Back to configuration</a></p>";
    out += "</body></html>";
    return out;
}

}  // namespace thermo::config_page
