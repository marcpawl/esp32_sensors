// Tests for the configuration UI JSON rendering (§6.1).
//
// render_scan_json is the wire contract for the on-device /scan endpoint, so
// its structure (key names, quoting, ordering) and its handling of missing
// values are asserted precisely. Sensor names come from the config page and
// are untrusted input (§5.1.3).
#include "unity.h"

#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

#include "app_config.hpp"
#include "config_page.hpp"
#include "https_server.hpp"
#include "power_source.hpp"
#include "sensor.hpp"

using thermo::Config;
using thermo::DS18B20Reading;
using thermo::PowerSource;
using thermo::RomId;
using thermo::ScanSnapshot;
using thermo::config_page::render_scan_json;

namespace {

// Builds a ROM ID whose first byte is `tag` so tests can distinguish sensors.
RomId rom_with_byte(std::uint8_t tag) {
    RomId rom{};
    rom.bytes[0] = tag;
    rom.bytes[7] = 0x28;  // DS18B20 family code, for realism.
    return rom;
}

DS18B20Reading reading_with_temp(std::uint8_t tag,
                                 std::optional<std::int16_t> temp) {
    DS18B20Reading r{};
    r.rom = rom_with_byte(tag);
    r.temp_c_x100 = temp;
    return r;
}

}  // namespace

// --- Empty snapshot ---------------------------------------------------------

TEST_CASE("scan json: no sensors yields empty array", "[config_page]") {
    Config cfg{};
    ScanSnapshot scan{};
    scan.power_source = PowerSource::kSolarBattery;

    const std::string json = render_scan_json(cfg, scan);

    TEST_ASSERT_EQUAL_STRING(
        "{\n"
        "  \"power_source\": \"solar battery\",\n"
        "  \"unit\": \"C\",\n"
        "  \"sensors\": [\n"
        "  ]\n"
        "}\n",
        json.c_str());
}

// --- power_source is a quoted JSON string (§6.1) ---------------------------

TEST_CASE("scan json: power source is a quoted string", "[config_page]") {
    Config cfg{};
    ScanSnapshot scan{};
    scan.power_source = PowerSource::kUsbComputer;

    const std::string json = render_scan_json(cfg, scan);

    TEST_ASSERT_NOT_NULL(
        strstr(json.c_str(), "\"power_source\": \"USB computer\""));
}

TEST_CASE("scan json: every power source maps to its quoted name",
          "[config_page]") {
    const struct {
        PowerSource source;
        const char* expected;
    } cases[] = {
        {PowerSource::kUnknown, "\"power_source\": \"Unknown\""},
        {PowerSource::kSolarBattery, "\"power_source\": \"solar battery\""},
        {PowerSource::kUsbBatteryPack,
         "\"power_source\": \"USB battery pack\""},
        {PowerSource::kUsbComputer, "\"power_source\": \"USB computer\""},
    };

    for (const auto& c : cases) {
        Config cfg{};
        ScanSnapshot scan{};
        scan.power_source = c.source;

        const std::string json = render_scan_json(cfg, scan);
        TEST_ASSERT_NOT_NULL_MESSAGE(strstr(json.c_str(), c.expected),
                                     c.expected);
    }
}

// --- battery_mv is optional -------------------------------------------------

TEST_CASE("scan json: battery_mv is emitted when present", "[config_page]") {
    Config cfg{};
    ScanSnapshot scan{};
    scan.power_source = PowerSource::kSolarBattery;
    scan.battery_mv = 3900;

    const std::string json = render_scan_json(cfg, scan);

    TEST_ASSERT_NOT_NULL(strstr(json.c_str(), "\"battery_mv\": 3900,"));
}

TEST_CASE("scan json: battery_mv defaults to absent", "[config_page]") {
    Config cfg{};
    ScanSnapshot scan{};
    scan.power_source = PowerSource::kUsbBatteryPack;

    // Default-constructed snapshot carries no battery reading (§6.1).
    TEST_ASSERT_FALSE(scan.battery_mv.has_value());

    const std::string json = render_scan_json(cfg, scan);

    TEST_ASSERT_NULL(strstr(json.c_str(), "battery_mv"));
}

TEST_CASE("scan json: battery_mv is omitted when explicitly absent",
          "[config_page]") {
    Config cfg{};
    ScanSnapshot scan{};
    scan.power_source = PowerSource::kUsbBatteryPack;
    scan.battery_mv = std::nullopt;

    const std::string json = render_scan_json(cfg, scan);

    TEST_ASSERT_NULL(strstr(json.c_str(), "battery_mv"));
}

// --- Sensor entries: ROM, name, temp ---------------------------------------

TEST_CASE("scan json: sensor name defaults to the ROM id", "[config_page]") {
    Config cfg{};  // No mappings.
    ScanSnapshot scan{};
    scan.power_source = PowerSource::kSolarBattery;
    scan.sensors.push_back(
        reading_with_temp(0x01, static_cast<std::int16_t>(2350)));

    const std::string json = render_scan_json(cfg, scan);

    const std::string rom = rom_with_byte(0x01).to_hex();
    const std::string expected = "{ \"rom\": \"" + rom + "\", \"name\": \"" +
                                 rom + "\", \"temp_c\": 23.5 }";
    TEST_ASSERT_NOT_NULL(strstr(json.c_str(), expected.c_str()));
}

TEST_CASE("scan json: mapped name overrides the ROM id", "[config_page]") {
    const RomId rom = rom_with_byte(0x02);

    Config cfg{};
    cfg.mappings.push_back({rom.to_hex(), "greenhouse"});

    ScanSnapshot scan{};
    scan.power_source = PowerSource::kSolarBattery;
    scan.sensors.push_back(
        reading_with_temp(0x02, static_cast<std::int16_t>(-437)));

    const std::string json = render_scan_json(cfg, scan);

    const std::string expected = "{ \"rom\": \"" + rom.to_hex() +
                                 "\", \"name\": \"greenhouse\", "
                                 "\"temp_c\": -4.4 }";
    TEST_ASSERT_NOT_NULL(strstr(json.c_str(), expected.c_str()));
}

TEST_CASE("scan json: missing temperature serializes as null",
          "[config_page]") {
    Config cfg{};
    ScanSnapshot scan{};
    scan.power_source = PowerSource::kSolarBattery;
    scan.sensors.push_back(reading_with_temp(0x03, std::nullopt));

    const std::string json = render_scan_json(cfg, scan);

    TEST_ASSERT_NOT_NULL(strstr(json.c_str(), "\"temp_c\": null"));
}

// --- Untrusted names are HTML-escaped (§5.1.3) ------------------------------

TEST_CASE("scan json: names are html-escaped", "[config_page]") {
    const RomId rom = rom_with_byte(0x04);

    Config cfg{};
    cfg.mappings.push_back({rom.to_hex(), "<b>&\"'</b>"});

    ScanSnapshot scan{};
    scan.power_source = PowerSource::kSolarBattery;
    scan.sensors.push_back(reading_with_temp(0x04, std::nullopt));

    const std::string json = render_scan_json(cfg, scan);

    TEST_ASSERT_NOT_NULL(strstr(
        json.c_str(), "\"name\": \"&lt;b&gt;&amp;&quot;&#39;&lt;/b&gt;\""));
}

// --- Multiple sensors are comma-separated ----------------------------------

TEST_CASE("scan json: multiple sensors are comma-separated", "[config_page]") {
    Config cfg{};
    ScanSnapshot scan{};
    scan.power_source = PowerSource::kSolarBattery;
    scan.sensors.push_back(reading_with_temp(0x05, 100));
    scan.sensors.push_back(reading_with_temp(0x06, 200));

    const std::string json = render_scan_json(cfg, scan);

    const std::string first =
        "\"rom\": \"" + rom_with_byte(0x05).to_hex() + "\"";
    const std::string second =
        "\"rom\": \"" + rom_with_byte(0x06).to_hex() + "\"";

    const std::size_t a = json.find(first);
    const std::size_t b = json.find(second);
    TEST_ASSERT_TRUE(a != std::string::npos);
    TEST_ASSERT_TRUE(b != std::string::npos);
    TEST_ASSERT_TRUE(a < b);

    // The first entry's closing brace is followed by a comma; the last entry
    // has no trailing comma (which would be invalid JSON).
    const std::size_t first_close = json.find(" }", a);
    TEST_ASSERT_EQUAL_CHAR(',', json[first_close + 2]);
    const std::size_t last_close = json.find(" }", b);
    TEST_ASSERT_EQUAL_CHAR('\n', json[last_close + 2]);
}

// --- Field order is stable --------------------------------------------------

TEST_CASE("scan json: top-level field order is stable", "[config_page]") {
    Config cfg{};
    ScanSnapshot scan{};
    scan.power_source = PowerSource::kSolarBattery;
    scan.battery_mv = 3700;

    const std::string json = render_scan_json(cfg, scan);

    const std::size_t power = json.find("\"power_source\"");
    const std::size_t batt = json.find("\"battery_mv\"");
    const std::size_t unit = json.find("\"unit\"");
    const std::size_t sensors = json.find("\"sensors\"");

    TEST_ASSERT_TRUE(power < batt);
    TEST_ASSERT_TRUE(batt < unit);
    TEST_ASSERT_TRUE(unit < sensors);
}
