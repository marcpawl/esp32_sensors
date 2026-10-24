// Shared, implementation-free types for `espidf_cpp::HttpServer`.
//
// This header holds the *vocabulary* the server and its handlers use — the
// request method, the fixed-size form-field containers, the response status,
// `HttpRequest`, and the `HttpHandler` callback type — and no ESP-IDF
// dependency, so it can be included (and unit tested) anywhere.
//
// Design constraints (see `http_server.hpp` for the full contract):
//
//   * **No dynamic memory.** Form fields are fixed-capacity, inline-storage
//     strings, and a handler writes its response body into the server's fixed
//     response buffer. A request that does not fit is answered with a 4xx
//     status, never aborted on.
//   * **No exceptions.** ESP-IDF builds with -fno-exceptions; nothing here
//     throws.
//   * **Plain data.** Every type below is copyable/movable and owns no
//     resources beyond its inline storage.
#pragma once

// NOLINTBEGIN
#pragma GCC diagnostic push  // NOLINT(ignored diagnostics)
#pragma GCC diagnostic pop
// NOLINTEND

#include <charconv>
#include <optional>
#include <span>
#include <string_view>
#include <system_error>
#include <type_traits>

#include <cstddef>
#include <cstdint>

#include "espidf_cpp/static_string.hpp"
#include "espidf_cpp/static_vector.hpp"

namespace thermo::espidf_cpp {

// --- Tunables ---------------------------------------------------------------
//
// All capacities are in bytes (form-field capacities in characters).

// Largest request body the server buffers. A larger `Content-Length` is
// answered with 413 Content Too Large *before* the body is read.
inline constexpr std::size_t kMaxHttpBodyBytes = 1024;

// Largest single form field name / value retained; longer fields are dropped
// (the request is still dispatched — see `HttpRequest::fields_truncated`).
inline constexpr std::size_t kMaxHttpFieldName = 32;
inline constexpr std::size_t kMaxHttpFieldValue = 128;

// Largest number of form fields retained per request.
inline constexpr std::size_t kMaxHttpFormFields = 16;

// Largest number of URL prefixes that can be registered at once.
inline constexpr std::size_t kMaxHttpRoutes = 8;

// Smallest practical response-body capacity (and the one used by the test
// suites): enough for short responses and for exercising a handler's
// overflow path.
inline constexpr std::size_t kHttpSmallBody = 256;

// Response-body capacity used by the firmware's HTTP server.
inline constexpr std::size_t kHttpBodyBytes = 4096;

// Largest normalized URL path (a registered prefix or a request path). Longer
// requests are answered with 414 URI Too Long.
inline constexpr std::size_t kMaxHttpUrl = 64;

// --- Request method ---------------------------------------------------------

// The request method a handler is asked to serve. Only GET and POST are
// dispatched; any other method gets 405 Method Not Allowed.
enum class HttpMethod : std::uint8_t {
    kGet,
    kPost,
};

// `"GET"` / `"POST"` — e.g. for logging.
[[nodiscard]] constexpr const char* to_string(HttpMethod method) noexcept {
    switch (method) {
        case HttpMethod::kGet:
            return "GET";
        case HttpMethod::kPost:
            return "POST";
    }
    return "?";
}

// --- Form fields ------------------------------------------------------------

// One submitted name/value pair, both URL-decoded (`%XX`, and `+` as space).
using HttpFieldName = static_string<kMaxHttpFieldName>;
using HttpFieldValue = static_string<kMaxHttpFieldValue>;

struct HttpField {
    HttpFieldName name;
    HttpFieldValue value;

    friend bool operator==(HttpField const&, HttpField const&) = default;
};

using HttpFields = static_vector<HttpField, kMaxHttpFormFields>;

// --- Request ----------------------------------------------------------------

// Everything a handler knows about one GET or POST request.
//
// `uri`, `body`, and every string in `fields` are borrowed from the server's
// request storage and are valid only for the duration of the handler call.
struct HttpRequest {
    // GET or POST.
    HttpMethod method = HttpMethod::kGet;

    // Normalized request path: the URI without its query string or trailing
    // '/', e.g. "/simulator" or "/simulator/reset" (not NUL-terminated).
    std::string_view uri{};

    // Raw request body (not NUL-terminated). Empty for GET, and for a POST
    // without a body.
    std::span<const std::byte> body{};

    // Submitted name/value pairs: the query string for GET, the
    // application/x-www-form-urlencoded body for POST.
    //
    // Duplicates are kept in submission order (`find_field()` returns the
    // first). At most `kMaxHttpFormFields` fields are retained, and a field
    // whose name or value is longer than `kMaxHttpFieldName` /
    // `kMaxHttpFieldValue` is dropped; either way `fields_truncated` is set and
    // the request is still dispatched. A malformed POST body (an invalid `%`
    // escape, or a Content-Type other than a form) is rejected before the
    // handler runs (400 / 415).
    HttpFields fields{};

    // A submitted field was dropped (too many fields, or a name/value that did
    // not fit). Handlers that must not act on a partial submission can reject
    // the request themselves.
    bool fields_truncated = false;

    // Returns the value of the first field named `name`, or nullopt.
    //
    // Ambiguous when several fields share the name — check
    // `fields_truncated`, or iterate `fields` to see all of them.
    [[nodiscard]] std::optional<std::string_view> find_field(
        std::string_view name) const noexcept {
        for (const HttpField& field : fields) {
            if (std::string_view(field.name.c_str(), field.name.size()) ==
                name) {
                return std::string_view(field.value.c_str(),
                                        field.value.size());
            }
        }
        return std::nullopt;
    }
};

// --- Response ---------------------------------------------------------------

// The response status a handler asks for. Anything else (e.g. a redirect) is
// served by the ESP-IDF-native server (`https_server.hpp`), not this one.
enum class HttpStatus : std::uint8_t {
    kOk = 0,                // 200 OK
    kBadRequest,            // 400 Bad Request
    kNotFound,              // 404 Not Found
    kMethodNotAllowed,      // 405 Method Not Allowed
    kPayloadTooLarge,       // 413 Content Too Large
    kUriTooLong,            // 414 URI Too Long
    kUnsupportedMediaType,  // 415 Unsupported Media Type
    kInternalServerError,   // 500 Internal Server Error
    kServiceUnavailable,    // 503 Service Unavailable
};

// Numeric status code (200, 400, ...).
[[nodiscard]] constexpr int to_code(HttpStatus status) noexcept {
    switch (status) {
        case HttpStatus::kOk:
            return 200;
        case HttpStatus::kBadRequest:
            return 400;
        case HttpStatus::kNotFound:
            return 404;
        case HttpStatus::kMethodNotAllowed:
            return 405;
        case HttpStatus::kPayloadTooLarge:
            return 413;
        case HttpStatus::kUriTooLong:
            return 414;
        case HttpStatus::kUnsupportedMediaType:
            return 415;
        case HttpStatus::kInternalServerError:
            return 500;
        case HttpStatus::kServiceUnavailable:
            return 503;
    }
    return 500;
}

// The status line reason phrase ("OK", "Bad Request", ...).
[[nodiscard]] constexpr const char* to_reason(HttpStatus status) noexcept {
    switch (status) {
        case HttpStatus::kOk:
            return "OK";
        case HttpStatus::kBadRequest:
            return "Bad Request";
        case HttpStatus::kNotFound:
            return "Not Found";
        case HttpStatus::kMethodNotAllowed:
            return "Method Not Allowed";
        case HttpStatus::kPayloadTooLarge:
            return "Content Too Large";
        case HttpStatus::kUriTooLong:
            return "URI Too Long";
        case HttpStatus::kUnsupportedMediaType:
            return "Unsupported Media Type";
        case HttpStatus::kInternalServerError:
            return "Internal Server Error";
        case HttpStatus::kServiceUnavailable:
            return "Service Unavailable";
    }
    return "Internal Server Error";
}

// --- Handler -----------------------------------------------------------------

// What a handler returns: the status and content type of its response. The
// body was written into the handler context's response buffer;
// `content_type` is borrowed (not copied), so pass a string literal.
struct HttpResponse {
    HttpStatus status = HttpStatus::kOk;
    std::string_view content_type = "text/plain";
};

// What a handler is called with: the request and the response buffer it writes
// its body into (`static_string<BodyCapacity>`). `body` always holds a valid,
// empty string on entry and has `BodyCapacity` characters available.
template<std::size_t BodyCapacity>
struct HttpHandlerContext {
    HttpRequest const& request;
    static_string<BodyCapacity>& body;
};

// The handler signature: a plain function pointer (no captures, no
// allocation) returning the response status and content type. A handler that
// sends no body leaves the response buffer empty.
template<std::size_t BodyCapacity>
using HttpHandler = HttpResponse (*)(HttpHandlerContext<BodyCapacity>&);

// --- Helpers -----------------------------------------------------------------

// Parses a non-negative decimal integer exactly (no sign, no trailing
// characters). Returns nullopt for an empty string, overflow, or any other
// character — e.g. for form values.
template<typename Int>
    requires(std::is_integral_v<Int> && !std::is_same_v<Int, bool> &&
             !std::is_same_v<Int, char>)
[[nodiscard]] constexpr std::optional<Int> parse_int(
    std::string_view text) noexcept {
    if (text.empty()) {
        return std::nullopt;
    }
    Int value{};
    const auto result =
        std::from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size()) {
        return std::nullopt;
    }
    return value;
}

// Copies the path of `url` — everything before the first '?' or '#' — into
// `out`, normalized: trailing '/' characters are dropped (except for the
// root) and a leading '/' is added when missing. Returns the normalized
// length.
//
// Registration normalizes the registered prefix with the same function, so
// "/simulator/", "simulator" and "/simulator?x" are all the same prefix, and a
// request for "/simulator/" or "/simulator?x=1" matches a prefix registered as
// "/simulator". The root is the empty string. Returns nullopt when the
// normalized path does not fit in `out` (the caller answers 414 URI Too Long).
[[nodiscard]] inline std::optional<std::size_t> normalize_url(
    std::string_view url, std::span<char> out) noexcept {
    std::string_view path = url.substr(0, url.find_first_of("?#"));
    while (path.size() > 1 && path.back() == '/') {
        path.remove_suffix(1);
    }
    if (path == "/") {
        path = {};
    }
    const bool needs_slash = !path.empty() && path.front() != '/';
    const std::size_t length = path.size() + (needs_slash ? 1U : 0U);
    if (length > out.size()) {
        return std::nullopt;
    }
    std::size_t pos = 0;
    if (needs_slash) {
        out[0] = '/';
        pos = 1;
    }
    for (std::size_t i = 0; i < path.size(); ++i) {
        out[pos + i] = path[i];
    }
    return length;
}

}  // namespace thermo::espidf_cpp
