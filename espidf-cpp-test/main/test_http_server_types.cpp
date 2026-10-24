// Tests for the `espidf_cpp::HttpServer` vocabulary
// (espidf_cpp/http_server_types.hpp).
//
// These types carry no ESP-IDF dependency and no server state: the request
// method, the fixed-capacity form fields, the response status, and the
// `parse_int()` / `normalize_url()` helpers the server uses to turn a raw URI
// into a route key. Everything here is pure logic and is exercised on the
// host. The server itself is covered by test_http_server.cpp.
#include "unity.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

#include "espidf_cpp/http_server_types.hpp"

namespace {

using namespace thermo::espidf_cpp;

// Normalizes `url` into `out` and returns the result as a string_view into it.
std::optional<std::string_view> normalize(std::string_view url) {
    static std::array<char, 32> buffer{};
    const std::optional<std::size_t> length =
        normalize_url(url, std::span<char>(buffer.data(), buffer.size()));
    if (!length) {
        return std::nullopt;
    }
    return std::string_view(buffer.data(), *length);
}

void assert_normalized(std::string_view url, std::string_view expected) {
    const std::optional<std::string_view> result = normalize(url);
    TEST_ASSERT_TRUE(result.has_value());
    TEST_ASSERT_EQUAL_STRING_LEN(expected.data(), result->data(),
                                 expected.size());
    TEST_ASSERT_EQUAL_UINT(expected.size(), result->size());
}

}  // namespace

// --- HttpMethod -------------------------------------------------------------

TEST_CASE("http_server_types: method names", "[http_server_types]") {
    TEST_ASSERT_EQUAL_STRING("GET", to_string(HttpMethod::kGet));
    TEST_ASSERT_EQUAL_STRING("POST", to_string(HttpMethod::kPost));
}

// --- HttpStatus -------------------------------------------------------------

TEST_CASE("http_server_types: status codes", "[http_server_types]") {
    TEST_ASSERT_EQUAL_INT(200, to_code(HttpStatus::kOk));
    TEST_ASSERT_EQUAL_INT(400, to_code(HttpStatus::kBadRequest));
    TEST_ASSERT_EQUAL_INT(404, to_code(HttpStatus::kNotFound));
    TEST_ASSERT_EQUAL_INT(405, to_code(HttpStatus::kMethodNotAllowed));
    TEST_ASSERT_EQUAL_INT(413, to_code(HttpStatus::kPayloadTooLarge));
    TEST_ASSERT_EQUAL_INT(414, to_code(HttpStatus::kUriTooLong));
    TEST_ASSERT_EQUAL_INT(415, to_code(HttpStatus::kUnsupportedMediaType));
    TEST_ASSERT_EQUAL_INT(500, to_code(HttpStatus::kInternalServerError));
    TEST_ASSERT_EQUAL_INT(503, to_code(HttpStatus::kServiceUnavailable));
}

TEST_CASE("http_server_types: status reason phrases", "[http_server_types]") {
    TEST_ASSERT_EQUAL_STRING("OK", to_reason(HttpStatus::kOk));
    TEST_ASSERT_EQUAL_STRING("Bad Request", to_reason(HttpStatus::kBadRequest));
    TEST_ASSERT_EQUAL_STRING("Content Too Large",
                             to_reason(HttpStatus::kPayloadTooLarge));
    TEST_ASSERT_EQUAL_STRING("Unsupported Media Type",
                             to_reason(HttpStatus::kUnsupportedMediaType));
    TEST_ASSERT_EQUAL_STRING("Internal Server Error",
                             to_reason(HttpStatus::kInternalServerError));
}

// --- normalize_url ----------------------------------------------------------

TEST_CASE("http_server_types: normalize keeps a plain path",
          "[http_server_types]") {
    assert_normalized("/simulator", "/simulator");
    assert_normalized("/simulator/reset", "/simulator/reset");
}

TEST_CASE("http_server_types: normalize adds a leading slash",
          "[http_server_types]") {
    assert_normalized("simulator", "/simulator");
}

TEST_CASE("http_server_types: normalize drops a trailing slash",
          "[http_server_types]") {
    assert_normalized("/simulator/", "/simulator");
    assert_normalized("/simulator///", "/simulator");
    assert_normalized("simulator/", "/simulator");
}

TEST_CASE("http_server_types: normalize drops the query string",
          "[http_server_types]") {
    assert_normalized("/simulator?x=1&y=2", "/simulator");
    assert_normalized("/simulator?x=/a/b", "/simulator");
    assert_normalized("/simulator#frag", "/simulator");
    assert_normalized("/sim/?a=b", "/sim");
}

TEST_CASE("http_server_types: normalize makes the root the empty path",
          "[http_server_types]") {
    assert_normalized("/", "");
    assert_normalized("", "");
    assert_normalized("?a=b", "");
}

TEST_CASE("http_server_types: normalize rejects a path that does not fit",
          "[http_server_types]") {
    std::array<char, 4> buffer{};
    const std::span<char> out(buffer.data(), buffer.size());
    TEST_ASSERT_TRUE(normalize_url("/abc", out).has_value());  // Exact fit.
    TEST_ASSERT_FALSE(
        normalize_url("/abcd", out).has_value());  // One too many.
    // The added leading slash counts against the capacity.
    TEST_ASSERT_FALSE(normalize_url("abcd", out).has_value());
}

// --- parse_int --------------------------------------------------------------

TEST_CASE("http_server_types: parse_int accepts decimal integers",
          "[http_server_types]") {
    TEST_ASSERT_EQUAL_INT(0, *parse_int<int>("0"));
    TEST_ASSERT_EQUAL_INT(42, *parse_int<int>("42"));
    TEST_ASSERT_EQUAL_UINT(65535, *parse_int<std::uint16_t>("65535"));
}

TEST_CASE("http_server_types: parse_int rejects anything else",
          "[http_server_types]") {
    TEST_ASSERT_FALSE(parse_int<int>("").has_value());
    TEST_ASSERT_FALSE(parse_int<int>(" 1").has_value());
    TEST_ASSERT_FALSE(parse_int<int>("1 ").has_value());
    TEST_ASSERT_FALSE(parse_int<int>("1x").has_value());
    TEST_ASSERT_FALSE(parse_int<int>("0x10").has_value());
    TEST_ASSERT_FALSE(parse_int<std::uint16_t>("65536").has_value());
    TEST_ASSERT_FALSE(parse_int<int>("99999999999999999999").has_value());
}

// --- HttpRequest ------------------------------------------------------------

TEST_CASE("http_server_types: find_field returns the first match",
          "[http_server_types]") {
    HttpRequest request;
    TEST_ASSERT_TRUE(request.fields.try_push_back(HttpField{"a", "1"}));
    TEST_ASSERT_TRUE(request.fields.try_push_back(HttpField{"b", "2"}));
    TEST_ASSERT_TRUE(request.fields.try_push_back(HttpField{"a", "3"}));

    const std::optional<std::string_view> first = request.find_field("a");
    TEST_ASSERT_TRUE(first.has_value());
    TEST_ASSERT_EQUAL_STRING_LEN("1", first->data(), first->size());

    const std::optional<std::string_view> missing = request.find_field("zzz");
    TEST_ASSERT_FALSE(missing.has_value());
}

TEST_CASE("http_server_types: a default request is a GET with no fields",
          "[http_server_types]") {
    const HttpRequest request;
    TEST_ASSERT_TRUE(request.method == HttpMethod::kGet);
    TEST_ASSERT_TRUE(request.uri.empty());
    TEST_ASSERT_TRUE(request.body.empty());
    TEST_ASSERT_TRUE(request.fields.empty());
    TEST_ASSERT_FALSE(request.fields_truncated);
}

TEST_CASE("http_server_types: fields are fixed-capacity",
          "[http_server_types]") {
    HttpField field{"name", "value"};
    TEST_ASSERT_EQUAL_UINT(kMaxHttpFieldName, field.name.capacity());
    TEST_ASSERT_EQUAL_UINT(kMaxHttpFieldValue, field.value.capacity());
    TEST_ASSERT_EQUAL_UINT(kMaxHttpFormFields, HttpFields::capacity());
}
