// Tests for batched-upload payload formatting (§9.1, §9.2, §9.3).
//
// The payload is the device's wire contract with the remote collector, so the
// structure (key names, ordering, array lengths) and escaping are asserted
// precisely. Names come from the config page and are untrusted input (§5.1.3).
#include "unity.h"

#include <cstring>
#include <string>
#include <vector>

#include "payload.hpp"
#include "temp_format.hpp"

using thermo::kTempNull;
using thermo::payload::build_batch_json;
using thermo::payload::NamedSample;

namespace {

NamedSample make_sample(std::uint32_t age_s, std::uint16_t batt_mv,
                        std::int16_t t1, std::int16_t t2, std::int16_t t3) {
    NamedSample s{};
    s.age_s = age_s;
    s.batt_mv = batt_mv;
    s.temps_c_x100 = {t1, t2, t3};
    return s;
}

std::vector<std::string> names(const std::string& a, const std::string& b,
                               const std::string& c) {
    return {a, b, c};
}

} // namespace

// --- Structure with no samples ---------------------------------------------

TEST_CASE("payload: empty batch has zero size and empty array",
          "[payload]") {
    const std::string json =
        build_batch_json("greenhouse", names("a", "b", "c"), {});

    TEST_ASSERT_EQUAL_STRING(
        "{\"device\":\"greenhouse\",\"unit\":\"C\",\"batt_unit\":\"mV\","
        "\"batch_size\":0,\"samples\":[]}",
        json.c_str());
}

// --- Single sample, fully populated ----------------------------------------

TEST_CASE("payload: single sample with names and values", "[payload]") {
    const std::vector<NamedSample> samples{
        make_sample(120, 3900, 2315, -437, 8500)};

    const std::string json =
        build_batch_json("greenhouse", names("soil", "air", "water"), samples);

    TEST_ASSERT_EQUAL_STRING(
        "{\"device\":\"greenhouse\",\"unit\":\"C\",\"batt_unit\":\"mV\","
        "\"batch_size\":1,\"samples\":[{\"age_s\":120,\"batt_mv\":3900,"
        "\"soil\":23.2,\"air\":-4.4,\"water\":85.0}]}",
        json.c_str());
}

// --- batch_size tracks the number of samples -------------------------------

TEST_CASE("payload: batch_size equals sample count", "[payload]") {
    const std::vector<NamedSample> samples{
        make_sample(60, 3800, 100, 200, 300),
        make_sample(120, 3810, 110, 210, 310),
        make_sample(180, 3820, 120, 220, 320),
    };

    const std::string json =
        build_batch_json("dev1", names("a", "b", "c"), samples);

    TEST_ASSERT_NOT_NULL(strstr(json.c_str(), "\"batch_size\":3"));
}

TEST_CASE("payload: multiple samples are comma-separated oldest-first",
          "[payload]") {
    const std::vector<NamedSample> samples{
        make_sample(60, 3800, 100, 0, 0),
        make_sample(120, 3810, 200, 0, 0),
    };

    const std::string json =
        build_batch_json("dev1", names("a", "b", "c"), samples);

    // First sample appears before the second in the document.
    const std::size_t first = json.find("\"age_s\":60");
    const std::size_t second = json.find("\"age_s\":120");
    TEST_ASSERT_TRUE(first != std::string::npos);
    TEST_ASSERT_TRUE(second != std::string::npos);
    TEST_ASSERT_TRUE(first < second);
}

// --- Null temperature handling (§4.1, §9.3) --------------------------------

TEST_CASE("payload: missing reading serializes as null", "[payload]") {
    const std::vector<NamedSample> samples{
        make_sample(30, 3700, kTempNull, 1234, kTempNull)};

    const std::string json =
        build_batch_json("dev1", names("a", "b", "c"), samples);

    TEST_ASSERT_NOT_NULL(strstr(json.c_str(), "\"a\":null"));
    TEST_ASSERT_NOT_NULL(strstr(json.c_str(), "\"b\":12.3"));
    TEST_ASSERT_NOT_NULL(strstr(json.c_str(), "\"c\":null"));
}

// --- Missing / empty names get synthesized keys (§9.1) ---------------------

TEST_CASE("payload: empty name falls back to sensorN key", "[payload]") {
    const std::vector<NamedSample> samples{
        make_sample(30, 3700, 1000, 2000, 3000)};

    const std::string json =
        build_batch_json("dev1", names("", "", ""), samples);

    TEST_ASSERT_NOT_NULL(strstr(json.c_str(), "\"sensor1\":10.0"));
    TEST_ASSERT_NOT_NULL(strstr(json.c_str(), "\"sensor2\":20.0"));
    TEST_ASSERT_NOT_NULL(strstr(json.c_str(), "\"sensor3\":30.0"));
}

TEST_CASE("payload: short names vector pads with sensorN keys", "[payload]") {
    const std::vector<NamedSample> samples{
        make_sample(30, 3700, 1000, 2000, 3000)};

    // Only one name supplied; the remaining two must still be emitted so the
    // per-sample key count stays constant (§9.2 column layout).
    const std::string json =
        build_batch_json("dev1", std::vector<std::string>{"soil"}, samples);

    TEST_ASSERT_NOT_NULL(strstr(json.c_str(), "\"soil\":10.0"));
    TEST_ASSERT_NOT_NULL(strstr(json.c_str(), "\"sensor2\":20.0"));
    TEST_ASSERT_NOT_NULL(strstr(json.c_str(), "\"sensor3\":30.0"));
}

// --- Untrusted names must be escaped ---------------------------------------

TEST_CASE("payload: device name is JSON-escaped", "[payload]") {
    const std::string json =
        build_batch_json("a\"b\\c", names("x", "y", "z"), {});

    TEST_ASSERT_NOT_NULL(strstr(json.c_str(), "\"device\":\"a\\\"b\\\\c\""));
}

TEST_CASE("payload: control characters are escaped as \\u00XX", "[payload]") {
    // NOTE: "\x01" "ctrl" uses literal concatenation so the hex escape does
    // not swallow the following 'c' (a hex escape consumes all hex digits).
    std::string name = "line\nbreak\tand\x01" "ctrl";

    const std::vector<NamedSample> samples{make_sample(1, 1, 1, 1, 1)};
    const std::string json =
        build_batch_json("dev", {name, "b", "c"}, samples);

    TEST_ASSERT_NOT_NULL(strstr(json.c_str(), "\\n"));
    TEST_ASSERT_NOT_NULL(strstr(json.c_str(), "\\t"));
    TEST_ASSERT_NOT_NULL(strstr(json.c_str(), "\\u0001"));
}

// --- Document is well-formed at the edges ----------------------------------

TEST_CASE("payload: every sample has exactly the expected keys",
          "[payload]") {
    const std::vector<NamedSample> samples{
        make_sample(5, 3750, 111, 222, 333)};
    const std::string json =
        build_batch_json("d", names("a", "b", "c"), samples);

    for (const char* key : {"\"age_s\":", "\"batt_mv\":", "\"a\":", "\"b\":",
                            "\"c\":"}) {
        TEST_ASSERT_NOT_NULL_MESSAGE(strstr(json.c_str(), key), key);
    }
}

TEST_CASE("payload: zero-age and zero-battery sample is valid", "[payload]") {
    const std::vector<NamedSample> samples{make_sample(0, 0, 0, 0, 0)};
    const std::string json = build_batch_json("d", names("a", "b", "c"), samples);

    TEST_ASSERT_NOT_NULL(strstr(json.c_str(), "\"age_s\":0"));
    TEST_ASSERT_NOT_NULL(strstr(json.c_str(), "\"batt_mv\":0"));
}
