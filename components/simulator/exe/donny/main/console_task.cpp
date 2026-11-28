// Interactive console task for the `donny` linux emulation (test driver).
//
// Protocol
// --------
// One JSON *object* per line, in both directions. Every field is optional on
// input: a missing field leaves that piece of board state alone, which lets a
// driver send `{"switch_config":"on"}` to close a switch without touching
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
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#pragma GCC diagnostic pop
// NOLINTEND

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include <errno.h>
#include <unistd.h>

#include "espidf_cpp/nvs.hpp"
#include "hal/ds18b20_types.hpp"
#include "hal/led_types.hpp"
#include "hal/wifi_types.hpp"
#include "logic/board.hpp"
#include "logic/reboot.hpp"
#include "logic/rom_id.hpp"
#include "simulator/restore_hardware.hpp"

// The vendored JSON library is not analyzed. `-fanalyzer` reports findings
// inside it (its `construct_at` path and standard-container internals) that
// belong to upstream code, not to this task.
// NOLINTBEGIN
#pragma GCC diagnostic push  // NOLINT(ignored diagnostics)
#pragma GCC diagnostic ignored "-Wanalyzer-malloc-leak"
#pragma GCC diagnostic ignored "-Wanalyzer-possible-null-dereference"
#pragma GCC diagnostic ignored "-Wanalyzer-use-of-uninitialized-value"
#include "nlohmann/json.hpp"
#pragma GCC diagnostic pop

// NOLINTEND

#include "../../../../espidfcpp/libespidfcpp/include/espidf_cpp/softswitch.hpp"
#include "../../../../espidfcpp/libespidfcpp/include/espidf_cpp/switch.hpp"
#include "../../../../logic/liblogic/include/logic/pin_map.hpp"
#include "espidf_cpp/switch.hpp"

namespace thermo::simulator {
namespace {

using json = nlohmann::json;

constexpr char const* const kTag = "console";

// Size of the line buffer. A full snapshot of the stub board is well under
// 1 KiB; the generous bound leaves room for a driver to send a longer document
// (for example a sensor list with long ROM IDs) without truncation.
constexpr std::size_t kLineCapacity = 4096;

// Consumes stdin up to and including the next newline (or end of input).
void discard_through_newline()
{
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
std::optional<std::string> read_line(std::array<char, kLineCapacity>& buffer)
{
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

constexpr const char* to_text(espidf_cpp::SwitchState state) noexcept
{
    return state == espidf_cpp::SwitchState::kOn ? "on" : "off";
}

constexpr const char* to_text(BoardSimulator::Power state)
{
    return state == BoardSimulator::Power::On ? "on" : "off";
}

// Parses "on" / "off" (or a JSON boolean). Returns `std::nullopt` for anything
// else, so a caller can report a bad value instead of guessing.
std::optional<bool> parse_switch(const json& value)
{
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

// Copies a JSON string into a fixed HAL credential buffer, keeping it
// NUL-terminated. Text longer than the field allows is a protocol error and is
// rejected (rather than truncated, which would silently change the credential).
template<std::size_t Capacity>
bool assign_text(std::array<char, Capacity>& buffer, const json& value,
                 std::size_t max_length = Capacity - 1)
{
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

// --- Snapshot: the board's state as JSON -----------------------------------

// The complete board state. Every field is always present.
json snapshot(const simulator::BoardSimulator& board)
{
    return json{
        {"power", to_text(board.power_)},
        {"switch_config", to_text(board.config_switch().read())},
        {"switch_run", to_text(board.run_switch().read())},
    };
}

espidf_cpp::SwitchState switch_state_from_bool(bool b)
{
    if (b) {
        return espidf_cpp::SwitchState::kOn;
    } else {
        return espidf_cpp::SwitchState::kOff;
    }
}

// --- Applying a document to the board --------------------------------------

// Applies the fields present in `doc`. Returns `std::nullopt` on success, or a
// message naming the first field that could not be applied. Fields are applied
// one at a time, so a rejected document takes effect partially; the caller
// reports the error and still echoes the resulting state, letting the driver
// see what did take effect.
std::optional<std::string> apply(const json& doc, BoardSimulator& board)
{
    if (!doc.is_object()) {
        return "document must be a JSON object";
    }

    espidf_cpp::Nvs<espidf_cpp::NvsMode::ReadWrite> nvs(
        simulator::kSwitchNamespace);
    bool reboot_needed = false;

    if (const auto it = doc.find("power"); it != doc.end()) {
        const auto power = parse_switch(*it);
        if (!power.has_value()) {
            return "power must be \"on\" or \"off\"";
        }
        const bool power_on = *power;
        if ((!power_on) && board.power_ == BoardSimulator::Power::On) {
            reboot_needed = true;
        }
        board.power_ =
            power_on ? BoardSimulator::Power::On : BoardSimulator::Power::Off;
        ESP_LOGI(kTag, "board_power %d", board.power_);
    }

    // Save the hardware state
    if (const auto it = doc.find("switch_config"); it != doc.end()) {
        const auto switch_opt = parse_switch(*it);
        if (switch_opt.has_value()) {
            espidf_cpp::SoftSwitch config(thermo::logic::pins::kSwitchConfig);
            espidf_cpp::SwitchState switch_state =
                switch_state_from_bool(switch_opt.value());
            config.set(switch_state);
            nvs.write(simulator::kConfigSwitchKey, switch_opt.value());
            ESP_LOGI(kTag, "switch_config %d", switch_opt.value());
        }
    }
    if (const auto it = doc.find("switch_run"); it != doc.end()) {
        const auto switch_opt = parse_switch(*it);
        if (switch_opt.has_value()) {
            espidf_cpp::SoftSwitch run(thermo::logic::pins::kSwitchRun);
            espidf_cpp::SwitchState switch_state =
                switch_state_from_bool(switch_opt.value());
            run.set(switch_state);
            nvs.write(simulator::kRunSwitchKey, switch_opt.value());
            ESP_LOGI(kTag, "switch_run %d", switch_opt.value());
        }
    }

    if (reboot_needed) {
        ESP_LOGI(kTag, "Powering off, rebooting.");
        std::fprintf(stdout, "MODEL %s\n", snapshot(board).dump().c_str());
        std::fflush(stdout);
        thermo::logic::reboot();
    }

    // `deep_sleep.last_s` is reported but ignored: the only way to set it is to
    // sleep, which would end this process.
    return std::nullopt;
}

}  // namespace

ConsoleRunner::ConsoleRunner(BoardSimulator& board) noexcept : board_(board) {}

// `[[noreturn]]`, as required by `espidf_cpp::Task<Runner>`.
[[noreturn]] void ConsoleRunner::run() noexcept
{
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
            std::fprintf(stdout, "MODEL {\"error\":\"malformed JSON\"}\n");
            std::fflush(stdout);
            continue;
        }

        json response;
        if (const auto error = apply(doc, board_); error.has_value()) {
            response = json{{"error", *error}, {"state", snapshot(board_)}};
        } else {
            response = snapshot(board_);
        }
        std::fprintf(stdout, "MODEL %s\n", response.dump().c_str());
        std::fflush(stdout);
    }
}

ConsoleTask::ConsoleTask(simulator::BoardSimulator& board)
    : espidf_cpp::Task<ConsoleRunner>(ConsoleRunner(board),
                                      espidf_cpp::TaskParams{.name = "console"})
{
}

}  // namespace thermo::simulator
