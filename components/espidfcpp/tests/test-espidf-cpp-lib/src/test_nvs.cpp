// Tests for `espidf_cpp::Nvs`.
//
// These cases run on both the host (`linux`) and the ESP32-S3 executor. NVS is
// initialized once, and the test namespace is cleared before every case so the
// cases do not depend on each other or on data left by an earlier run.
//
// Coverage:
//
//   * write/read round trips for uint8/uint16/uint32, int8/int16/int32,
//     float, static_string, and bool;
//   * missing keys returning the caller's default;
//   * empty strings, strings at the maximum length, and the length reported
//     when a stored string is longer than the caller's buffer;
//   * two handles open at the same time, including two writers;
//   * the static `init_nvs_flash()` and `deinit_nvs_flash()` entry points
//     for both modes;
//   * the compile-time contract that `Nvs<NvsMode::Read>` has no writes.

#include "espidf_cpp/nvs.hpp"

// NOLINTBEGIN
#pragma GCC diagnostic push  // NOLINT(ignored diagnostics)
#include "nvs.h"
#pragma GCC diagnostic pop
// NOLINTEND

#include <concepts>
#include <cstddef>
#include <cstdint>

#include "unity.h"
#include "utility/static_string.hpp"

namespace {

using thermo::espidf_cpp::Nvs;
using thermo::espidf_cpp::NvsMode;
using thermo::utility::static_string;

constexpr char kNamespace[] = "nvs_cpp_test";

template<NvsMode mode, typename T>
concept HasNumericWrite = requires(Nvs<mode>& nvs) { nvs.write("key", T{}); };

template<NvsMode mode, std::size_t Capacity>
concept HasStringWrite =
    requires(Nvs<mode>& nvs, static_string<Capacity> const& value) {
        nvs.write("key", value);
    };

static_assert(!HasNumericWrite<NvsMode::Read, std::uint8_t>);
static_assert(!HasNumericWrite<NvsMode::Read, float>);
static_assert(!HasStringWrite<NvsMode::Read, 8>);
static_assert(HasNumericWrite<NvsMode::ReadWrite, std::uint8_t>);
static_assert(HasNumericWrite<NvsMode::ReadWrite, bool>);
static_assert(HasNumericWrite<NvsMode::ReadWrite, float>);
static_assert(HasStringWrite<NvsMode::ReadWrite, 8>);

void clear_namespace() {
    // Constructing an Nvs initializes flash before opening the namespace.
    {
        Nvs<NvsMode::ReadWrite> init{kNamespace};
    }
    nvs_handle_t handle = 0;
    ESP_ERROR_CHECK(nvs_open(kNamespace, NVS_READWRITE, &handle));
    ESP_ERROR_CHECK(nvs_erase_all(handle));
    ESP_ERROR_CHECK(nvs_commit(handle));
    nvs_close(handle);
}

}  // namespace

// --- Initialization ---------------------------------------------------------

TEST_CASE("nvs: init and deinit are available for both modes", "[nvs]") {
    Nvs<NvsMode::Read>::init_nvs_flash();
    Nvs<NvsMode::ReadWrite>::init_nvs_flash();
    Nvs<NvsMode::Read>::deinit_nvs_flash();
    Nvs<NvsMode::ReadWrite>::deinit_nvs_flash();
}

TEST_CASE("nvs: deinit_nvs_flash allows a fresh init", "[nvs]") {
    Nvs<NvsMode::ReadWrite>::init_nvs_flash();
    Nvs<NvsMode::ReadWrite>::deinit_nvs_flash();

    clear_namespace();
    Nvs<NvsMode::ReadWrite> nvs{kNamespace};
    nvs.write("after_deinit", std::uint8_t{0x5AU});
    TEST_ASSERT_EQUAL_HEX8(0x5AU, nvs.read("after_deinit", std::uint8_t{0}));
}

// --- Boolean ----------------------------------------------------------------

TEST_CASE("nvs: bool write and read round trip", "[nvs]") {
    clear_namespace();
    Nvs<NvsMode::ReadWrite> nvs{kNamespace};
    nvs.write("true", true);
    nvs.write("false", false);

    TEST_ASSERT_TRUE(nvs.read("true", false));
    TEST_ASSERT_FALSE(nvs.read("false", true));
    TEST_ASSERT_TRUE(nvs.read("absent_true", true));
    TEST_ASSERT_FALSE(nvs.read("absent_false", false));
}

// --- Unsigned integers ------------------------------------------------------

TEST_CASE("nvs: uint8_t write and read round trip", "[nvs]") {
    clear_namespace();
    Nvs<NvsMode::ReadWrite> nvs{kNamespace};
    nvs.write("u8", std::uint8_t{0xA5U});
    TEST_ASSERT_EQUAL_HEX8(0xA5U, nvs.read("u8", std::uint8_t{0}));
}

TEST_CASE("nvs: uint16_t write and read round trip", "[nvs]") {
    clear_namespace();
    Nvs<NvsMode::ReadWrite> nvs{kNamespace};
    nvs.write("u16", std::uint16_t{0xBEEFU});
    TEST_ASSERT_EQUAL_HEX16(0xBEEFU, nvs.read("u16", std::uint16_t{0}));
}

TEST_CASE("nvs: uint32_t write and read round trip", "[nvs]") {
    clear_namespace();
    Nvs<NvsMode::ReadWrite> nvs{kNamespace};
    nvs.write("u32", std::uint32_t{0xDEADBEEFU});
    TEST_ASSERT_EQUAL_HEX32(0xDEADBEEFU, nvs.read("u32", std::uint32_t{0}));
}

// --- Signed integers --------------------------------------------------------

TEST_CASE("nvs: int8_t write and read round trip", "[nvs]") {
    clear_namespace();
    Nvs<NvsMode::ReadWrite> nvs{kNamespace};
    nvs.write("i8", std::int8_t{-42});
    TEST_ASSERT_EQUAL_INT8(-42, nvs.read("i8", std::int8_t{0}));
}

TEST_CASE("nvs: int16_t write and read round trip", "[nvs]") {
    clear_namespace();
    Nvs<NvsMode::ReadWrite> nvs{kNamespace};
    nvs.write("i16", std::int16_t{-12345});
    TEST_ASSERT_EQUAL_INT16(-12345, nvs.read("i16", std::int16_t{0}));
}

TEST_CASE("nvs: int32_t write and read round trip", "[nvs]") {
    clear_namespace();
    Nvs<NvsMode::ReadWrite> nvs{kNamespace};
    nvs.write("i32", std::int32_t{-123456789});
    TEST_ASSERT_EQUAL_INT32(-123456789, nvs.read("i32", std::int32_t{0}));
}

// --- Float ------------------------------------------------------------------

TEST_CASE("nvs: float write and read round trip", "[nvs]") {
    clear_namespace();
    Nvs<NvsMode::ReadWrite> nvs{kNamespace};
    nvs.write("f", -12.5F);
    TEST_ASSERT_EQUAL_FLOAT(-12.5F, nvs.read("f", 0.0F));
}

// --- Defaults for absent keys -----------------------------------------------

TEST_CASE("nvs: a missing numeric key returns the default", "[nvs]") {
    clear_namespace();
    Nvs<NvsMode::ReadWrite> nvs{kNamespace};
    TEST_ASSERT_EQUAL_UINT8(11U, nvs.read("absent_u8", std::uint8_t{11}));
    TEST_ASSERT_EQUAL_UINT16(2222U,
                             nvs.read("absent_u16", std::uint16_t{2222}));
    TEST_ASSERT_EQUAL_UINT32(3333333U,
                             nvs.read("absent_u32", std::uint32_t{3333333}));
    TEST_ASSERT_EQUAL_INT8(-4, nvs.read("absent_i8", std::int8_t{-4}));
    TEST_ASSERT_EQUAL_INT16(-555, nvs.read("absent_i16", std::int16_t{-555}));
    TEST_ASSERT_EQUAL_INT32(-66666,
                            nvs.read("absent_i32", std::int32_t{-66666}));
    TEST_ASSERT_EQUAL_FLOAT(7.25F, nvs.read("absent_f", 7.25F));
}

TEST_CASE("nvs: a missing string key returns the default", "[nvs]") {
    clear_namespace();
    Nvs<NvsMode::ReadWrite> nvs{kNamespace};
    const auto value = nvs.read<16>("absent_str", "fallback");
    TEST_ASSERT_EQUAL_STRING("fallback", value.c_str());
    TEST_ASSERT_EQUAL_UINT(8, value.size());
}

// --- Strings ----------------------------------------------------------------

TEST_CASE("nvs: an empty string round trips", "[nvs]") {
    clear_namespace();
    Nvs<NvsMode::ReadWrite> nvs{kNamespace};
    nvs.write("empty", static_string<8>{""});
    const auto value = nvs.read<8>("empty", "fallback");
    TEST_ASSERT_TRUE(value.empty());
    TEST_ASSERT_EQUAL_STRING("", value.c_str());
}

TEST_CASE("nvs: a string at maximum length round trips", "[nvs]") {
    clear_namespace();
    Nvs<NvsMode::ReadWrite> nvs{kNamespace};
    constexpr char kMax[] = "12345678";
    nvs.write("max", static_string<8>{kMax});
    const auto value = nvs.read<8>("max", "fallback");
    TEST_ASSERT_EQUAL_STRING(kMax, value.c_str());
    TEST_ASSERT_EQUAL_UINT(8, value.size());
}

TEST_CASE("nvs: a longer stored string reports the length needed to read it",
          "[nvs]") {
    clear_namespace();
    Nvs<NvsMode::ReadWrite> nvs{kNamespace};
    nvs.write("long", static_string<12>{"abcdefghijkl"});

    nvs_handle_t handle = 0;
    ESP_ERROR_CHECK(nvs_open(kNamespace, NVS_READONLY, &handle));
    std::size_t len = 0;
    ESP_ERROR_CHECK(nvs_get_str(handle, "long", nullptr, &len));
    TEST_ASSERT_EQUAL_UINT(13, len);  // 12 characters plus the NUL terminator.
    char small[9] = {};
    len = sizeof(small);
    TEST_ASSERT_EQUAL(ESP_ERR_NVS_INVALID_LENGTH,
                      nvs_get_str(handle, "long", small, &len));
    TEST_ASSERT_EQUAL_UINT(13, len);
    nvs_close(handle);
}

// --- Two handles ------------------------------------------------------------

TEST_CASE("nvs: two handles opened at once see the same values", "[nvs]") {
    clear_namespace();
    Nvs<NvsMode::ReadWrite> writer{kNamespace};
    Nvs<NvsMode::Read> reader{kNamespace};
    writer.write("shared", std::uint32_t{0x12345678U});
    TEST_ASSERT_EQUAL_HEX32(0x12345678U,
                            reader.read("shared", std::uint32_t{0}));
    writer.write("shared_str", static_string<8>{"visible"});
    TEST_ASSERT_EQUAL_STRING("visible",
                             reader.read<8>("shared_str", "fallback").c_str());
}

TEST_CASE("nvs: two writers opened at once see each other's writes", "[nvs]") {
    clear_namespace();
    Nvs<NvsMode::ReadWrite> first{kNamespace};
    Nvs<NvsMode::ReadWrite> second{kNamespace};

    // Distinct keys keep the test independent of last-writer-wins behavior.
    first.write("first_u32", std::uint32_t{0xA5A5A5A5U});
    first.write("first_str", static_string<8>{"one"});
    second.write("second_u32", std::uint32_t{0x5A5A5A5AU});
    second.write("second_str", static_string<8>{"two"});

    TEST_ASSERT_EQUAL_HEX32(0xA5A5A5A5U,
                            first.read("first_u32", std::uint32_t{0}));
    TEST_ASSERT_EQUAL_HEX32(0xA5A5A5A5U,
                            second.read("first_u32", std::uint32_t{0}));
    TEST_ASSERT_EQUAL_HEX32(0x5A5A5A5AU,
                            first.read("second_u32", std::uint32_t{0}));
    TEST_ASSERT_EQUAL_HEX32(0x5A5A5A5AU,
                            second.read("second_u32", std::uint32_t{0}));

    TEST_ASSERT_EQUAL_STRING("one",
                             first.read<8>("first_str", "fallback").c_str());
    TEST_ASSERT_EQUAL_STRING("one",
                             second.read<8>("first_str", "fallback").c_str());
    TEST_ASSERT_EQUAL_STRING("two",
                             first.read<8>("second_str", "fallback").c_str());
    TEST_ASSERT_EQUAL_STRING("two",
                             second.read<8>("second_str", "fallback").c_str());
}
