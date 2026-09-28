// Tests for DS18B20 ROM-ID hex formatting and parsing (§5.1.3, §6.1).
//
// ROM IDs are the stable identity used to attach friendly names to sensors in
// the configuration, so the textual form must round-trip exactly and reject
// malformed input rather than silently producing a wrong identity.
#include "unity.h"

#include <string>

#include "constants.hpp"
#include "sensor.hpp"

using thermo::RomId;

namespace {

RomId make_rom(std::initializer_list<unsigned> bytes) {
    RomId id{};
    std::size_t i = 0;
    for (unsigned b : bytes) {
        if (i < id.bytes.size()) {
            id.bytes[i++] = static_cast<std::uint8_t>(b);
        }
    }
    return id;
}

} // namespace

// --- Formatting -------------------------------------------------------------

TEST_CASE("rom: to_hex emits 16 uppercase characters", "[rom_id]") {
    const RomId id = make_rom({0x28, 0xFF, 0x64, 0x1D, 0x00, 0x00, 0x00, 0x7A});
    const std::string hex = id.to_hex();
    TEST_ASSERT_EQUAL_UINT(16, hex.size());
    TEST_ASSERT_EQUAL_STRING("28FF641D0000007A", hex.c_str());
}

TEST_CASE("rom: leading zeros are preserved", "[rom_id]") {
    const RomId id = make_rom({0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07});
    TEST_ASSERT_EQUAL_STRING("0001020304050607", id.to_hex().c_str());
}

TEST_CASE("rom: all-0xFF and all-zero are distinct", "[rom_id]") {
    const RomId ones = make_rom({0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF});
    const RomId zeros = make_rom({0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00});
    TEST_ASSERT_EQUAL_STRING("FFFFFFFFFFFFFFFF", ones.to_hex().c_str());
    TEST_ASSERT_EQUAL_STRING("0000000000000000", zeros.to_hex().c_str());
    TEST_ASSERT_FALSE(ones == zeros);
}

// --- Parsing ----------------------------------------------------------------

TEST_CASE("rom: from_hex round-trips uppercase", "[rom_id]") {
    const RomId original =
        make_rom({0x28, 0xFF, 0x64, 0x1D, 0x00, 0x00, 0x00, 0x7A});
    const auto parsed = RomId::from_hex("28FF641D0000007A");
    TEST_ASSERT_TRUE(parsed.has_value());
    TEST_ASSERT_TRUE(original == *parsed);
    TEST_ASSERT_EQUAL_STRING("28FF641D0000007A", parsed->to_hex().c_str());
}

TEST_CASE("rom: from_hex accepts lowercase and mixed case", "[rom_id]") {
    const auto lower = RomId::from_hex("28ff641d0000007a");
    const auto mixed = RomId::from_hex("28Ff641D0000007a");
    const auto upper = RomId::from_hex("28FF641D0000007A");

    TEST_ASSERT_TRUE(lower.has_value());
    TEST_ASSERT_TRUE(mixed.has_value());
    TEST_ASSERT_TRUE(upper.has_value());
    TEST_ASSERT_TRUE(*lower == *upper);
    TEST_ASSERT_TRUE(*mixed == *upper);
}

TEST_CASE("rom: from_hex rejects wrong lengths", "[rom_id]") {
    TEST_ASSERT_FALSE(RomId::from_hex("").has_value());
    TEST_ASSERT_FALSE(RomId::from_hex("28FF").has_value());            // 4
    TEST_ASSERT_FALSE(RomId::from_hex("28FF641D000000").has_value());  // 14
    TEST_ASSERT_FALSE(RomId::from_hex("28FF641D0000007A0").has_value()); // 17
}

TEST_CASE("rom: from_hex rejects non-hex characters", "[rom_id]") {
    TEST_ASSERT_FALSE(RomId::from_hex("28FF641D0000007G").has_value()); // G
    TEST_ASSERT_FALSE(RomId::from_hex("28FF641D0000007 ").has_value()); // space
    TEST_ASSERT_FALSE(RomId::from_hex("28FF641D-000007A").has_value()); // dash
    // 0x prefix or colon separators are not accepted.
    TEST_ASSERT_FALSE(RomId::from_hex("0x28FF641D00007A").has_value());
    TEST_ASSERT_FALSE(RomId::from_hex("28:FF:641D00007A").has_value());
}

TEST_CASE("rom: canonical form is uppercase for config keys",
          "[rom_id]") {
    // §5.1.3 stores keys as map_<ROMID> in uppercase; normalizing through
    // from_hex(...)->to_hex() must always yield the canonical key.
    const auto parsed = RomId::from_hex("28ff641d0000007a");
    TEST_ASSERT_TRUE(parsed.has_value());
    TEST_ASSERT_EQUAL_STRING("28FF641D0000007A", parsed->to_hex().c_str());
}
