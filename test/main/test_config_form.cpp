// Tests for the configuration form parsing and Config conversion (§5.1, §5.3).
//
// POST /save hands the router a URL-encoded body; config_form turns it into a
// candidate Config. These tests pin the decoding, the field mapping, and the
// "start from the current config" merge semantics the save path relies on.
#include "unity.h"

#include <cstdint>
#include <string>
#include <vector>

#include "app_config.hpp"
#include "config_form.hpp"

using thermo::Config;
using thermo::config_form::Field;
using thermo::config_form::get;
using thermo::config_form::parse;
using thermo::config_form::to_config;
using thermo::config_form::url_decode;

// --- url_decode -------------------------------------------------------------

TEST_CASE("url_decode: percent escapes are decoded", "[config_form]") {
    TEST_ASSERT_EQUAL_STRING("a b/c", url_decode("a%20b%2Fc", true).c_str());
    TEST_ASSERT_EQUAL_STRING("A", url_decode("%41", true).c_str());
    TEST_ASSERT_EQUAL_STRING("~", url_decode("%7E", true).c_str());
}

TEST_CASE("url_decode: plus maps to space only when requested",
          "[config_form]") {
    TEST_ASSERT_EQUAL_STRING("a b", url_decode("a+b", true).c_str());
    TEST_ASSERT_EQUAL_STRING("a+b", url_decode("a+b", false).c_str());
}

TEST_CASE("url_decode: literal characters pass through", "[config_form]") {
    TEST_ASSERT_EQUAL_STRING("hello-world_1.2",
                             url_decode("hello-world_1.2", true).c_str());
}

// --- parse ------------------------------------------------------------------

TEST_CASE("parse: splits on & and =", "[config_form]") {
    const std::vector<Field> fields = parse("a=1&b=2&c=3");
    TEST_ASSERT_EQUAL_UINT(3, fields.size());
    TEST_ASSERT_EQUAL_STRING("a", fields[0].first.c_str());
    TEST_ASSERT_EQUAL_STRING("1", fields[0].second.c_str());
    TEST_ASSERT_EQUAL_STRING("c", fields[2].first.c_str());
    TEST_ASSERT_EQUAL_STRING("3", fields[2].second.c_str());
}

TEST_CASE("parse: values are url-decoded", "[config_form]") {
    const std::vector<Field> fields = parse("ssid=my+ap&url=http%3A%2F%2Fx");
    TEST_ASSERT_EQUAL_STRING("my ap", fields[0].second.c_str());
    TEST_ASSERT_EQUAL_STRING("http://x", fields[1].second.c_str());
}

TEST_CASE("parse: value containing = is preserved", "[config_form]") {
    const std::vector<Field> fields = parse("x=a=b");
    TEST_ASSERT_EQUAL_UINT(1, fields.size());
    TEST_ASSERT_EQUAL_STRING("a=b", fields[0].second.c_str());
}

TEST_CASE("parse: pair without '=' is skipped", "[config_form]") {
    const std::vector<Field> fields = parse("novalue&a=1");
    TEST_ASSERT_EQUAL_UINT(1, fields.size());
    TEST_ASSERT_EQUAL_STRING("a", fields[0].first.c_str());
}

TEST_CASE("parse: empty body yields no fields", "[config_form]") {
    TEST_ASSERT_TRUE(parse("").empty());
}

TEST_CASE("parse: empty value is kept", "[config_form]") {
    const std::vector<Field> fields = parse("a=");
    TEST_ASSERT_EQUAL_UINT(1, fields.size());
    TEST_ASSERT_EQUAL_STRING("", fields[0].second.c_str());
}

// --- get --------------------------------------------------------------------

TEST_CASE("get: returns the first matching value", "[config_form]") {
    const std::vector<Field> fields = parse("a=1&a=2");
    TEST_ASSERT_EQUAL_STRING("1", get(fields, "a").value().c_str());
}

TEST_CASE("get: missing key is nullopt", "[config_form]") {
    const std::vector<Field> fields = parse("a=1");
    TEST_ASSERT_FALSE(get(fields, "b").has_value());
}

// --- to_config: merge semantics ---------------------------------------------

TEST_CASE("to_config: untouched fields survive", "[config_form]") {
    Config current{};
    current.ap_ssid = "keep-me";
    current.interval_s = 1234;

    // Only ap_pass is submitted.
    const Config candidate = to_config(current, parse("ap_pass=secret"));

    TEST_ASSERT_EQUAL_STRING("keep-me", candidate.ap_ssid.c_str());
    TEST_ASSERT_EQUAL_UINT32(1234, candidate.interval_s);
    TEST_ASSERT_EQUAL_STRING("secret", candidate.ap_pass.c_str());
}

TEST_CASE("to_config: numeric fields are parsed", "[config_form]") {
    Config current{};
    const Config candidate =
        to_config(current, parse("interval_s=60&batt_low_mv=3300"
                                 "&batt_cal_offset=-12&batt_cal_gain=1.02"));

    TEST_ASSERT_EQUAL_UINT32(60, candidate.interval_s);
    TEST_ASSERT_EQUAL_UINT32(3300, candidate.batt_low_mv);
    TEST_ASSERT_EQUAL_INT32(-12, candidate.batt_cal_offset);
    TEST_ASSERT_EQUAL_FLOAT(1.02f, candidate.batt_cal_gain);
}

TEST_CASE("to_config: non-numeric value falls back to current",
          "[config_form]") {
    Config current{};
    current.interval_s = 999;
    const Config candidate = to_config(current, parse("interval_s=abc"));
    TEST_ASSERT_EQUAL_UINT32(999, candidate.interval_s);
}

TEST_CASE("to_config: checkbox enabled only when the field is present",
          "[config_form]") {
    Config current{};
    current.batt_gate_en = false;
    TEST_ASSERT_TRUE(to_config(current, parse("batt_gate_en=1")).batt_gate_en);

    current.batt_gate_en = true;
    TEST_ASSERT_FALSE(to_config(current, parse("other=1")).batt_gate_en);
}

TEST_CASE("to_config: map_ fields update existing names", "[config_form]") {
    Config current{};
    current.mappings.push_back({"28FF01", "old"});

    const Config candidate = to_config(current, parse("map_28FF01=outside"));
    TEST_ASSERT_EQUAL_UINT(1, candidate.mappings.size());
    TEST_ASSERT_EQUAL_STRING("outside", candidate.mappings[0].name.c_str());
}

TEST_CASE("to_config: map_ fields add new names", "[config_form]") {
    Config current{};
    const Config candidate = to_config(current, parse("map_28FF02=greenhouse"));
    TEST_ASSERT_EQUAL_UINT(1, candidate.mappings.size());
    TEST_ASSERT_EQUAL_STRING("28FF02", candidate.mappings[0].rom.c_str());
    TEST_ASSERT_EQUAL_STRING("greenhouse", candidate.mappings[0].name.c_str());
}
