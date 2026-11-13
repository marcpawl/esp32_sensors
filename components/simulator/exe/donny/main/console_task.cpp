// Interactive console task for the `donny` linux emulation (test driver).
//
// Protocol
// --------
// One JSON *object* per line, in both directions. Every field is optional on
// input: a missing field leaves that piece of board state alone, which lets a
// driver send `{"config_button":"on"}` to press a button without touching
// anything else. On output every field is always present, so the document is a
// complete snapshot of the board.
//
// The round trip is idempotent: feeding the task's own output back in changes
// nothing and yields byte-identical output. Three reported fields are therefore
// deliberately *ignored on input*, because nothing in the board's interface can
// set them: `wifi.initialized` (only `init()` sets it), `wifi.ap` /
// `wifi.station` (see below), and `deep_sleep.last_s` (the only way to sleep is
// to end the process).
//
// `wifi.ap` and `wifi.station` are reported only while the interface is
// *running*, because the HAL can record a configuration solely by starting the
// interface (`start_ap()` / `start_station()`). A stopped interface therefore
// reports `null`, and re-starting it reuses whatever configuration the stub
// still holds. That is what makes a stop/start round trip stable.
//
// stdout carries only these protocol lines. ESP-IDF's log output goes to stdout
// by default (`esp_log_vprint_func = &vprintf`), so `app_main` redirects it to
// stderr; otherwise a stub device logging while a line is applied would corrupt
// the document.
#include "console_task.hpp"

// NOLINTBEGIN
#pragma GCC diagnostic push  // NOLINT(ignored diagnostics)
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#pragma GCC diagnostic pop
// NOLINTEND

#include <array>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include <errno.h>
#include <unistd.h>

#include "hal/ds18b20_types.hpp"
#include "hal/led_types.hpp"
#include "hal/push_button_types.hpp"
#include "hal/wifi_types.hpp"
#include "logic/rom_id.hpp"
#include "nlohmann/json.hpp"

namespace thermo {
namespace {

using json = nlohmann::json;

constexpr char const* const kTag = "console";

// Size of the line buffer. A full snapshot of the stub board is well under
// 1 KiB; the generous bound leaves room for a driver to send a longer document
// (for example a sensor list with long ROM IDs) without truncation.
constexpr std::size_t kLineCapacity = 4096;

// Consumes stdin up to and including the next newline (or end of input).
void discard_through_newline() {
    while (true) {
        char discarded = '\0';
        const ssize_t count = ::read(STDIN_FILENO, &discarded, 1);
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count <= 0 || discarded == '\n') {
            return;
        }
    }
}

// Reads one line from stdin into `buffer`, without the newline.
//
// Returns `std::nullopt` on end of input, and `std::string{}` for a line that
// was too long (the excess is drained so the next read starts at a line
// boundary, which the caller ignores) rather than silently truncating a
// document.
//
// This is a hand-written reader rather than `std::fgets` because on the ESP-IDF
// `linux` target the FreeRTOS port drives the scheduler from a 1 kHz
// `SIGALRM` and installs that handler with `sa_flags = 0` — no `SA_RESTART`.
// A signal with no `SA_RESTART` makes any blocking read return early, and
// `fgets` cannot tell "interrupted" from "end of file": it sets the sticky
// error flag and returns `NULL`. Reading interactively therefore looked exactly
// like EOF, and the task shut its input down ~1 ms after it started (a pipe
// only appeared to work because the data was already buffered). `read()` keeps
// `errno`, so an `EINTR` can simply be resumed — and it is also what clears a
// spurious `stdin` error flag, since that flag lives on the `FILE` and the
// handle is read directly here.
std::optional<std::string> read_line(std::array<char, kLineCapacity>& buffer) {
    std::size_t length = 0;
    while (true) {
        // One byte at a time, so nothing past the newline is consumed: bytes
        // that arrive together (a write-then-read driver) stay available for
        // the next call. Reads are only made when the line is incomplete, so
        // the usual case costs no extra syscall.
        const ssize_t count = ::read(STDIN_FILENO, &buffer[length], 1);
        if (count < 0) {
            if (errno == EINTR) {
                // The scheduler tick interrupted us; this is not end of input.
                continue;
            }
            ESP_LOGE(kTag, "stdin read failed: %s", std::strerror(errno));
            return std::nullopt;
        }
        if (count == 0) {
            // End of input. A final line without a newline is still delivered.
            if (length == 0) {
                return std::nullopt;
            }
            break;
        }
        if (buffer[length] == '\n') {
            break;
        }
        ++length;
        if (length == buffer.size()) {
            // The line is longer than any document the protocol allows. Discard
            // the remainder so the next read starts at a line boundary.
            discard_through_newline();
            ESP_LOGW(kTag, "discarded an over-long line");
            return std::string{};
        }
    }
    // Tolerate CRLF from a driver on another platform.
    if (length > 0 && buffer[length - 1] == '\r') {
        --length;
    }
    return std::string{buffer.data(), length};
}

// --- Small value helpers ---------------------------------------------------

constexpr const char* to_text(hal::ButtonState state) noexcept {
    return state == hal::ButtonState::kOn ? "on" : "off";
}

constexpr const char* to_text(hal::LedState state) noexcept {
    return state == hal::LedState::kOn ? "on" : "off";
}

// Parses "on" / "off" (or a JSON boolean). Returns `std::nullopt` for anything
// else, so a caller can report a bad value instead of guessing.
std::optional<bool> parse_switch(const json& value) {
    if (value.is_boolean()) {
        return value.get<bool>();
    }
    if (value.is_string()) {
        const std::string text = value.get<std::string>();
        if (text == "on") {
            return true;
        }
        if (text == "off") {
            return false;
        }
    }
    return std::nullopt;
}

// Parses a 16-bit temperature in hundredths of a degree. Rejects anything that
// is not an integer in range, so a driver cannot quietly wrap a value around.
std::optional<std::int16_t> parse_int16(const json& value) {
    if (!value.is_number_integer()) {  // signed or unsigned
        return std::nullopt;
    }
    if (value.is_number_unsigned()) {
        const auto number = value.get<std::uint64_t>();
        if (number > static_cast<std::uint64_t>(INT16_MAX)) {
            return std::nullopt;
        }
        return static_cast<std::int16_t>(number);
    }
    const auto number = value.get<std::int64_t>();
    if (number < INT16_MIN || number > INT16_MAX) {
        return std::nullopt;
    }
    return static_cast<std::int16_t>(number);
}

// Parses an unsigned byte (0-255) from a JSON integer.
std::optional<std::uint8_t> parse_u8(const json& value) {
    const auto number = parse_int16(value);
    if (!number.has_value() || *number < 0 || *number > 255) {
        return std::nullopt;
    }
    return static_cast<std::uint8_t>(*number);
}

// Copies a JSON string into a fixed HAL credential buffer, keeping it
// NUL-terminated. Text longer than the field allows is a protocol error and is
// rejected (rather than truncated, which would silently change the credential).
template<std::size_t Capacity>
bool assign_text(std::array<char, Capacity>& buffer, const json& value,
                 std::size_t max_length = Capacity - 1) {
    if (!value.is_string()) {
        return false;
    }
    const std::string text = value.get<std::string>();
    if (text.size() > max_length || text.size() >= buffer.size()) {
        return false;
    }
    buffer.fill('\0');
    std::memcpy(buffer.data(), text.data(), text.size());
    return true;
}

// Looks up the optional object field `key` inside `object`. A missing key and
// an explicit `null` both leave `out` null; anything that is not an object is
// an error, returned as the message to report. `key` is the field name to look
// for and `label` is how the error names it (the fully qualified path, e.g.
// "wifi.ap"), so the two are separate: the `wifi` object's keys are `ap` and
// `station`, not `wifi.ap` and `wifi.station`.
std::optional<std::string> find_object(const json& object, const char* key,
                                       const char* label, const json*& out) {
    out = nullptr;
    const auto it = object.find(key);
    if (it == object.end() || it->is_null()) {
        return std::nullopt;
    }
    if (!it->is_object()) {
        return std::string{label} + " must be an object or null";
    }
    out = &*it;
    return std::nullopt;
}

// Looks up the optional "on"/"off" field `key` inside `object`, following the
// same present/null/error rules as `find_object`.
std::optional<std::string> find_switch(const json& object, const char* key,
                                       const char* label,
                                       std::optional<bool>& out) {
    out.reset();
    const auto it = object.find(key);
    if (it == object.end() || it->is_null()) {
        return std::nullopt;
    }
    out = parse_switch(*it);
    if (!out.has_value()) {
        return std::string{label} + " must be \"on\" or \"off\"";
    }
    return std::nullopt;
}

// --- Snapshot: the board's state as JSON -----------------------------------

json sensors_json(const hal::BoardStub& board) {
    json sensors = json::array();
    for (const hal::DS18B20Reading& reading : board.one_wire().read()) {
        // `null` for a missing reading, matching the §9.3 sentinel's meaning.
        json temp = nullptr;
        if (reading.temp_c_x100.has_value()) {
            temp = *reading.temp_c_x100;
        }
        sensors.push_back(
            json{{"rom", rom_id_to_hex(reading.rom)}, {"temp_c_x100", temp}});
    }
    return sensors;
}

// The WiFi state. A configuration is reported only while its interface is
// running: the HAL can only record one by starting it, so that is the only
// state a driver can round-trip back in.
json wifi_json(const hal::BoardStub& board) {
    const hal::WifiRadio& wifi = board.wifi();

    json ap = nullptr;
    if (wifi.ap_running()) {
        if (const auto& cfg = wifi.ap_config(); cfg.has_value()) {
            ap = json{{"ssid", cfg->ssid.data()},
                      {"password", cfg->password.data()},
                      {"channel", cfg->channel},
                      {"max_connections", cfg->max_connections},
                      {"hidden", cfg->hidden}};
        }
    }

    json station = nullptr;
    if (wifi.station_running()) {
        if (const auto& cfg = wifi.station_config(); cfg.has_value()) {
            station = json{{"ssid", cfg->ssid.data()},
                           {"password", cfg->password.data()}};
        }
    }

    return json{{"initialized", wifi.initialized()},
                {"ap_running", wifi.ap_running()},
                {"station_running", wifi.station_running()},
                {"ap", std::move(ap)},
                {"station", std::move(station)}};
}

// The complete board state. Every field is always present.
json snapshot(const hal::BoardStub& board) {
    json deep_sleep = nullptr;
    if (const auto& slept = board.last_deep_sleep(); slept.has_value()) {
        deep_sleep = json{{"last_s", slept->count()}};
    }

    return json{
        {"button", to_text(board.button().read())},
        {"config_button", to_text(board.config_button().read())},
        {"run_button", to_text(board.run_button().read())},
        {"leds", json{{"green", to_text(board.green().state())},
                      {"red", to_text(board.red().state())}}},
        {"chip_temp_c_x100", board.chip_temperature().read()},
        {"sensors", sensors_json(board)},
        {"wifi", wifi_json(board)},
        {"deep_sleep", std::move(deep_sleep)},
    };
}

// --- Applying a document to the board --------------------------------------

// Overlays the `wifi.ap` fields present in `doc` onto `cfg`.
std::optional<std::string> apply_ap(const json& doc,
                                    hal::AccessPointConfig& cfg) {
    if (const auto it = doc.find("ssid");
        it != doc.end() && !assign_text(cfg.ssid, *it, hal::kMaxSsidLength)) {
        return "wifi.ap.ssid must be a string of at most 32 characters";
    }
    if (const auto it = doc.find("password");
        it != doc.end() &&
        !assign_text(cfg.password, *it, hal::kMaxPasswordLength)) {
        return "wifi.ap.password must be a string of at most 63 characters";
    }
    if (const auto it = doc.find("channel"); it != doc.end()) {
        const auto channel = parse_u8(*it);
        if (!channel.has_value()) {
            return "wifi.ap.channel must be an integer 0-255";
        }
        cfg.channel = *channel;
    }
    if (const auto it = doc.find("max_connections"); it != doc.end()) {
        const auto max_connections = parse_u8(*it);
        if (!max_connections.has_value()) {
            return "wifi.ap.max_connections must be an integer 0-255";
        }
        cfg.max_connections = *max_connections;
    }
    if (const auto it = doc.find("hidden"); it != doc.end()) {
        if (!it->is_boolean()) {
            return "wifi.ap.hidden must be a boolean";
        }
        cfg.hidden = it->get<bool>();
    }
    return std::nullopt;
}

// Overlays the `wifi.station` fields present in `doc` onto `cfg`.
std::optional<std::string> apply_station(const json& doc,
                                         hal::StationConfig& cfg) {
    if (const auto it = doc.find("ssid");
        it != doc.end() && !assign_text(cfg.ssid, *it, hal::kMaxSsidLength)) {
        return "wifi.station.ssid must be a string of at most 32 characters";
    }
    if (const auto it = doc.find("password");
        it != doc.end() &&
        !assign_text(cfg.password, *it, hal::kMaxPasswordLength)) {
        return "wifi.station.password must be a string of at most 63 "
               "characters";
    }
    if (!cfg.has_ssid()) {
        return "wifi.station needs an ssid";
    }
    return std::nullopt;
}

// Applies the `wifi` object. `initialized` is reported but ignored (only
// `init()` sets it).
//
// A configuration can only be applied *by starting* the interface, because that
// is the only operation the HAL offers that records one. Naming a
// configuration therefore brings the interface up; `*_running` on its own
// starts or stops it with the configuration already recorded. Resolving each
// configuration before its running state keeps the result independent of the
// order the driver wrote the keys in.
std::optional<std::string> apply_wifi(const json& wifi, hal::BoardStub& board) {
    if (!wifi.is_object()) {
        return "wifi must be an object";
    }

    // --- Access point ---
    const json* ap = nullptr;
    if (auto error = find_object(wifi, "ap", "wifi.ap", ap);
        error.has_value()) {
        return error;
    }
    std::optional<bool> ap_running;
    if (auto error =
            find_switch(wifi, "ap_running", "wifi.ap_running", ap_running);
        error.has_value()) {
        return error;
    }
    if (ap != nullptr) {
        if (ap_running.has_value() && !*ap_running) {
            return "wifi.ap cannot be applied while wifi.ap_running is off";
        }
        hal::AccessPointConfig cfg =
            board.wifi().ap_config().value_or(hal::AccessPointConfig{});
        if (auto error = apply_ap(*ap, cfg); error.has_value()) {
            return error;
        }
        if (board.wifi().start_ap(cfg) != ESP_OK) {
            return "wifi.ap could not be applied";
        }
    } else if (ap_running.has_value()) {
        if (*ap_running) {
            const hal::AccessPointConfig cfg =
                board.wifi().ap_config().value_or(hal::AccessPointConfig{});
            if (board.wifi().start_ap(cfg) != ESP_OK) {
                return "wifi.ap_running could not be applied";
            }
        } else if (board.wifi().stop_ap() != ESP_OK) {
            return "wifi.ap_running could not be applied";
        }
    }

    // --- Station ---
    const json* station = nullptr;
    if (auto error = find_object(wifi, "station", "wifi.station", station);
        error.has_value()) {
        return error;
    }
    std::optional<bool> station_running;
    if (auto error = find_switch(wifi, "station_running",
                                 "wifi.station_running", station_running);
        error.has_value()) {
        return error;
    }
    if (station != nullptr) {
        if (station_running.has_value() && !*station_running) {
            return "wifi.station cannot be applied while wifi.station_running "
                   "is off";
        }
        hal::StationConfig cfg =
            board.wifi().station_config().value_or(hal::StationConfig{});
        if (auto error = apply_station(*station, cfg); error.has_value()) {
            return error;
        }
        if (board.wifi().start_station(cfg) != ESP_OK) {
            return "wifi.station could not be applied";
        }
    } else if (station_running.has_value()) {
        if (*station_running) {
            const hal::StationConfig cfg =
                board.wifi().station_config().value_or(hal::StationConfig{});
            if (board.wifi().start_station(cfg) != ESP_OK) {
                return "wifi.station_running could not be applied";
            }
        } else if (board.wifi().stop_station() != ESP_OK) {
            return "wifi.station_running could not be applied";
        }
    }

    return std::nullopt;
}

// Applies the fields present in `doc`. Returns `std::nullopt` on success, or a
// message naming the first field that could not be applied. Fields are applied
// one at a time, so a rejected document takes effect partially; the caller
// reports the error and still echoes the resulting state, letting the driver
// see what did take effect.
std::optional<std::string> apply(const json& doc, hal::BoardStub& board) {
    if (!doc.is_object()) {
        return "document must be a JSON object";
    }

    // Buttons.
    for (const auto& [key, button] :
         {std::pair{"button", &board.button()},
          std::pair{"config_button", &board.config_button()},
          std::pair{"run_button", &board.run_button()}}) {
        const auto it = doc.find(key);
        if (it == doc.end()) {
            continue;
        }
        const auto pressed = parse_switch(*it);
        if (!pressed.has_value()) {
            return std::string{key} + " must be \"on\" or \"off\"";
        }
        button->set(*pressed ? hal::ButtonState::kOn : hal::ButtonState::kOff);
    }

    // LEDs. `write()` is the only mutator the HAL offers (and it logs).
    if (const auto leds = doc.find("leds"); leds != doc.end()) {
        if (!leds->is_object()) {
            return "leds must be an object";
        }
        for (const auto& [key, led] : {std::pair{"green", &board.green()},
                                       std::pair{"red", &board.red()}}) {
            const auto it = leds->find(key);
            if (it == leds->end()) {
                continue;
            }
            const auto lit = parse_switch(*it);
            if (!lit.has_value()) {
                return std::string{"leds."} + key +
                       " must be \"on\" or \"off\"";
            }
            led->write(*lit ? hal::LedState::kOn : hal::LedState::kOff);
        }
    }

    // Chip temperature.
    if (const auto it = doc.find("chip_temp_c_x100"); it != doc.end()) {
        const auto value = parse_int16(*it);
        if (!value.has_value()) {
            return "chip_temp_c_x100 must be an integer in int16 range";
        }
        board.chip_temperature().set_celsius(*value);
    }

    // Sensors: a complete replacement of the bus contents.
    if (const auto it = doc.find("sensors"); it != doc.end()) {
        if (!it->is_array()) {
            return "sensors must be an array";
        }
        std::vector<hal::DS18B20Reading> readings;
        readings.reserve(it->size());
        for (const json& entry : *it) {
            if (!entry.is_object()) {
                return "each sensor must be an object";
            }
            const auto rom_field = entry.find("rom");
            if (rom_field == entry.end() || !rom_field->is_string()) {
                return "each sensor needs a string \"rom\"";
            }
            const auto rom = rom_id_from_hex(rom_field->get<std::string>());
            if (!rom.has_value()) {
                return "sensor \"rom\" must be 16 hex characters";
            }
            hal::DS18B20Reading reading{};
            reading.rom = *rom;
            if (const auto temp = entry.find("temp_c_x100");
                temp != entry.end() && !temp->is_null()) {
                const auto value = parse_int16(*temp);
                if (!value.has_value()) {
                    return "sensor \"temp_c_x100\" must be an integer in int16 "
                           "range or null";
                }
                reading.temp_c_x100 = *value;
            }
            readings.push_back(reading);
        }
        board.one_wire().set_sensors(std::move(readings));
    }

    // WiFi.
    if (const auto wifi = doc.find("wifi"); wifi != doc.end()) {
        if (auto error = apply_wifi(*wifi, board); error.has_value()) {
            return error;
        }
    }

    // `deep_sleep.last_s` is reported but ignored: the only way to set it is to
    // sleep, which would end this process.
    return std::nullopt;
}

}  // namespace

ConsoleRunner::ConsoleRunner(std::shared_ptr<hal::BoardStub> board) noexcept
    : board_(std::move(board)) {}

// `[[noreturn]]`, as required by `espidf_cpp::Task<Runner>`.
[[noreturn]] void ConsoleRunner::run() noexcept {
    std::array<char, kLineCapacity> buffer{};

    while (true) {
        const std::optional<std::string> line = read_line(buffer);
        if (!line.has_value()) {
            // EOF: the driver closed the pipe. Nothing more can arrive, so
            // stop reading and idle instead of spinning on stdin (which would
            // burn a core and flood the log with protocol errors).
            while (true) {
                vTaskDelay(pdMS_TO_TICKS(60 * 1000));
            }
        }

        if (line->empty()) {
            // Blank line (or an over-long one, already drained): ignore it so
            // a driver may use blank lines as separators.
            continue;
        }

        // Parse without exceptions: this project builds with
        // `CONFIG_COMPILER_CXX_EXCEPTIONS=n`, and a malformed line from a
        // driver must never take the task down.
        const json doc = json::parse(*line, nullptr, false);
        if (doc.is_discarded()) {
            std::fprintf(stdout, "{\"error\":\"malformed JSON\"}\n");
            std::fflush(stdout);
            continue;
        }

        json response;
        if (const auto error = apply(doc, *board_); error.has_value()) {
            response = json{{"error", *error}, {"state", snapshot(*board_)}};
        } else {
            response = snapshot(*board_);
        }
        std::fprintf(stdout, "%s\n", response.dump().c_str());
        std::fflush(stdout);
    }
}

ConsoleTask::ConsoleTask(std::shared_ptr<hal::BoardStub> board)
    : espidf_cpp::Task<ConsoleRunner>(
          ConsoleRunner(std::move(board)),
          espidf_cpp::TaskParams{.name = "console"}) {}

}  // namespace thermo
