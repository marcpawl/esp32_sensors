// HttpServer<BodyCapacity> implementation (ESP-IDF esp_http_server backend).
//
// The ESP-IDF server is configured with ONE URI handler, "/*", registered for
// every method; each request is dispatched from there to the matching prefix
// route. That is what makes registering and unregistering routes a pure
// bookkeeping operation at run time (no httpd_config_t rebuild, no
// httpd_unregister_uri_handler, no handle juggling).
//
// Server start/stop is the only place that allocates (ESP-IDF's own state plus
// the request context). Request handling uses the pre-allocated request
// context, so a malformed or oversized request costs no allocation and never
// aborts — it turns into an HTTP error response.
#include "espidf_cpp/http_server.hpp"

#include <array>
#include <cstring>
#include <memory>
#include <new>
#include <optional>
#include <string_view>

#include "esp_log.h"

namespace thermo::espidf_cpp {
namespace {

constexpr const char* kTag = "http_server";

// Value of the Content-Type header that marks a URL-encoded form body.
constexpr const char* kFormContentType = "application/x-www-form-urlencoded";

// Longest Content-Type request-header value examined (it is only compared
// against `kFormContentType` plus parameters).
constexpr std::size_t kMaxContentTypeHeader = 64;

// Longest response content type sent to the client.
constexpr std::size_t kMaxContentType = 64;

// Capacity of the "NNN Reason Phrase" status line. The longest phrase used
// here ("Internal Server Error", 21 chars) plus "500 " is 25 characters.
constexpr std::size_t kMaxStatusLine = 32;

// The HTTP method tokens ESP-IDF stores in `httpd_req_t::method`.
constexpr int kMethodGet = static_cast<int>(HTTP_GET);
constexpr int kMethodPost = static_cast<int>(HTTP_POST);

// --- Response helpers -------------------------------------------------------

// Builds an HTTP status line ("404 Not Found") in `out`.
void make_status_line(static_string<kMaxStatusLine>& out,
                      HttpStatus status) noexcept {
    out.clear();
    int code = to_code(status);
    char digits[8];
    std::size_t count = 0;
    do {
        digits[count++] = static_cast<char>('0' + code % 10);
        code /= 10;
    } while (code > 0);
    while (count > 0) {
        out.push_back(digits[--count]);
    }
    out.push_back(' ');
    out.append(to_reason(status));
}

// Sends `body` with `status` and `content_type`. Both strings only need to
// stay valid for the duration of the call; an empty content type means
// "text/plain".
esp_err_t send_response(httpd_req_t* req, HttpStatus status,
                        std::string_view content_type,
                        std::string_view body) noexcept {
    static_string<kMaxStatusLine> status_line;
    make_status_line(status_line, status);
    httpd_resp_set_status(req, status_line.c_str());

    if (content_type.empty()) {
        content_type = "text/plain";
    }
    if (content_type.size() > kMaxContentType) {
        // A content type is expected to be a literal; clip rather than fail.
        content_type = content_type.substr(0, kMaxContentType);
    }
    static_string<kMaxContentType> type;
    type.append(content_type.data(), content_type.size());
    httpd_resp_set_type(req, type.c_str());

    return httpd_resp_send(req, body.data(), static_cast<ssize_t>(body.size()));
}

// --- Form decoding -----------------------------------------------------------

// Case-insensitive ASCII compare of the media type in `header` with
// `kFormContentType`, ignoring parameters ("; charset=UTF-8") and surrounding
// whitespace.
bool is_form_content_type(std::string_view header) noexcept {
    std::string_view media = header.substr(0, header.find(';'));
    while (!media.empty() && (media.back() == ' ' || media.back() == '\t')) {
        media.remove_suffix(1);
    }
    while (!media.empty() && (media.front() == ' ' || media.front() == '\t')) {
        media.remove_prefix(1);
    }
    const std::string_view expected(kFormContentType);
    if (media.size() != expected.size()) {
        return false;
    }
    for (std::size_t i = 0; i < media.size(); ++i) {
        char a = media[i];
        if (a >= 'A' && a <= 'Z') {
            a = static_cast<char>(a + ('a' - 'A'));
        }
        if (a != expected[i]) {
            return false;
        }
    }
    return true;
}

constexpr int hex_digit(char h) noexcept {
    if (h >= '0' && h <= '9') {
        return h - '0';
    }
    if (h >= 'a' && h <= 'f') {
        return h - 'a' + 10;
    }
    if (h >= 'A' && h <= 'F') {
        return h - 'A' + 10;
    }
    return -1;
}

enum class DecodeResult {
    kDecoded,    // `out` holds the decoded text.
    kTooLong,    // The text does not fit in `out`.
    kMalformed,  // A '%' escape is truncated or not hexadecimal.
};

// Decodes one `application/x-www-form-urlencoded` component ("a+b%21" ->
// "a b!") into `out`. With `plus_as_space == false` (query strings), '+' is
// kept literally. Never aborts: failures are reported through the result.
template<std::size_t FieldCapacity>
DecodeResult url_decode(std::string_view in, bool plus_as_space,
                        static_string<FieldCapacity>& out) noexcept {
    out.clear();
    // A component can never decode to more characters than it has bytes.
    if (in.size() > FieldCapacity) {
        return DecodeResult::kTooLong;
    }
    for (std::size_t i = 0; i < in.size(); ++i) {
        const char c = in[i];
        if (c == '%') {
            if (i + 2 >= in.size()) {
                out.clear();
                return DecodeResult::kMalformed;  // Truncated escape.
            }
            const int hi = hex_digit(in[i + 1]);
            const int lo = hex_digit(in[i + 2]);
            if (hi < 0 || lo < 0) {
                out.clear();
                return DecodeResult::kMalformed;
            }
            out.push_back(static_cast<char>((hi << 4) | lo));
            i += 2;
        } else {
            out.push_back(c == '+' && plus_as_space ? ' ' : c);
        }
    }
    return DecodeResult::kDecoded;
}

// Parses "name=value&name=value..." into `fields`.
//
// * A pair without '=' is skipped (malformed, but not fatal).
// * A field larger than its storage, and every field past `kMaxHttpFormFields`,
//   is dropped and sets `truncated`; the request is still dispatched with the
//   fields that did fit.
// * Any malformed '%' escape makes the whole form invalid (returns false).
bool parse_form(std::string_view data, bool plus_as_space, HttpFields& fields,
                bool& truncated) noexcept {
    HttpField field{};
    std::size_t pos = 0;
    while (pos < data.size()) {
        std::size_t end = data.find('&', pos);
        if (end == std::string_view::npos) {
            end = data.size();
        }
        const std::string_view pair = data.substr(pos, end - pos);
        pos = end + 1;

        if (pair.empty()) {
            continue;
        }
        const std::size_t eq = pair.find('=');
        if (eq == std::string_view::npos) {
            truncated = true;  // Malformed pair: keep the rest.
            continue;
        }
        if (fields.full()) {
            truncated = true;
            continue;
        }

        const DecodeResult name_result =
            url_decode(pair.substr(0, eq), plus_as_space, field.name);
        const DecodeResult value_result =
            url_decode(pair.substr(eq + 1), plus_as_space, field.value);
        if (name_result == DecodeResult::kMalformed ||
            value_result == DecodeResult::kMalformed) {
            return false;
        }
        if (name_result == DecodeResult::kTooLong ||
            value_result == DecodeResult::kTooLong) {
            truncated = true;
            continue;
        }
        fields.push_back(field);
    }
    return true;
}

}  // namespace

// --- Route matching ---------------------------------------------------------

template<std::size_t BodyCapacity>
bool HttpServer<BodyCapacity>::matches(std::string_view prefix,
                                       std::string_view path) noexcept {
    if (prefix.empty()) {
        return true;  // The root route covers everything.
    }
    if (path.size() < prefix.size() ||
        path.substr(0, prefix.size()) != prefix) {
        return false;
    }
    // The prefix must end on a path boundary: "/sim" covers "/sim" and
    // "/sim/x", but not "/simulator".
    return path.size() == prefix.size() || path[prefix.size()] == '/';
}

// --- Construction / destruction ----------------------------------------------

template<std::size_t BodyCapacity>
HttpServer<BodyCapacity>::~HttpServer() {
    stop();
}

template<std::size_t BodyCapacity>
void HttpServer<BodyCapacity>::destroy_context() noexcept {
    if (context_) {
        context_->fields().~HttpFields();
        context_.reset();
    }
}

// --- Registration
// -------------------------------------------------------------

template<std::size_t BodyCapacity>
esp_err_t HttpServer<BodyCapacity>::register_route(
    std::string_view prefix, HttpHandler<BodyCapacity> handler) noexcept {
    if (handle_ != nullptr) {
        ESP_LOGW(kTag, "register_route: server is running");
        return ESP_ERR_INVALID_STATE;
    }
    if (handler == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    std::array<char, kMaxHttpUrl> normalized{};
    const std::optional<std::size_t> length = normalize_url(
        prefix, std::span<char>(normalized.data(), normalized.size()));
    if (!length) {
        ESP_LOGW(kTag, "register_route: prefix too long");
        return ESP_ERR_INVALID_ARG;
    }
    const std::string_view key(normalized.data(), *length);

    // Re-registering a prefix replaces its handler.
    for (std::size_t i = 0; i < count_; ++i) {
        if (std::string_view(routes_[i].prefix.data(),
                             routes_[i].prefix.size()) == key) {
            routes_[i].handler = handler;
            return ESP_OK;
        }
    }

    if (count_ == routes_.size()) {
        ESP_LOGW(kTag, "register_route: no free route slot");
        return ESP_ERR_NO_MEM;
    }

    routes_[count_].prefix.clear();
    routes_[count_].prefix.append(normalized.data(), *length);
    routes_[count_].handler = handler;
    ++count_;
    return ESP_OK;
}

template<std::size_t BodyCapacity>
esp_err_t HttpServer<BodyCapacity>::unregister_route(
    std::string_view prefix) noexcept {
    if (handle_ != nullptr) {
        ESP_LOGW(kTag, "unregister_route: server is running");
        return ESP_ERR_INVALID_STATE;
    }

    std::array<char, kMaxHttpUrl> normalized{};
    const std::optional<std::size_t> length = normalize_url(
        prefix, std::span<char>(normalized.data(), normalized.size()));
    if (!length) {
        return ESP_ERR_NOT_FOUND;
    }
    const std::string_view key(normalized.data(), *length);

    // Remove exactly the routes the prefix covers — what "unregistering a
    // prefix" means for requests: "/sim" removes "/sim" and "/sim/*", but not
    // "/simulator". A route nested below the prefix is matched in the other
    // direction ("/sim" covers a route registered as "/sim/reset"), and the
    // empty root prefix matches every route.
    std::size_t kept = 0;
    std::size_t removed = 0;
    for (std::size_t i = 0; i < count_; ++i) {
        if (matches(key, std::string_view(routes_[i].prefix.data(),
                                          routes_[i].prefix.size()))) {
            ++removed;
        } else {
            if (kept != i) {
                routes_[kept] = routes_[i];
            }
            ++kept;
        }
    }
    count_ = kept;
    return removed > 0 ? ESP_OK : ESP_ERR_NOT_FOUND;
}

// --- Server lifecycle --------------------------------------------------------

template<std::size_t BodyCapacity>
HttpServer<BodyCapacity>::RequestContext::RequestContext() {
    // The field vector lives in raw storage; construct it once, in place. It
    // is destroyed by destroy_context().
    new (&fields_storage) HttpFields();
}

template<std::size_t BodyCapacity>
esp_err_t HttpServer<BodyCapacity>::start() noexcept {
    if (handle_ != nullptr) {
        return ESP_OK;
    }
    if (count_ == 0) {
        ESP_LOGE(kTag, "start: no routes registered");
        return ESP_ERR_INVALID_STATE;
    }

    // The request context (and the field vector constructed inside it).
    // start() is noexcept, so a failed allocation must become an error code
    // rather than terminate().
    context_.reset(new (std::nothrow) RequestContext());
    if (!context_) {
        ESP_LOGE(kTag, "start: out of memory for the request context");
        return ESP_ERR_NO_MEM;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = port_;
    // The ESP-IDF default control-socket port (32768) is a single static value
    // shared by every server, so a second HttpServer (or a restart before the
    // previous instance has torn down) would fail httpd_start() with
    // EADDRINUSE on the control socket. Derive it from the serving port
    // instead; each server then owns its own pair of ports.
    config.ctrl_port = static_cast<std::uint16_t>(port_ + 1);
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.max_uri_handlers = 2;

    httpd_handle_t handle = nullptr;
    esp_err_t err = httpd_start(&handle, &config);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "httpd_start failed: %s", esp_err_to_name(err));
        destroy_context();
        return err;
    }
    handle_ = handle;

    // Every registered route is served by the one catch-all handler.
    httpd_uri_t any = {};
    any.uri = "/*";
    // HTTP_ANY (INT_MAX) covers every method, so nothing falls through to the
    // ESP-IDF 404 handler before this one sees it.
    any.method = static_cast<enum http_method>(HTTP_ANY);
    any.handler = &HttpServer::handle_request;
    any.user_ctx = this;
    err = httpd_register_uri_handler(handle_, &any);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "httpd_register_uri_handler failed: %s",
                 esp_err_to_name(err));
        stop();
        return err;
    }

    ESP_LOGI(kTag, "listening on port %u with %u route(s)",
             static_cast<unsigned>(port_), static_cast<unsigned>(count_));
    return ESP_OK;
}

template<std::size_t BodyCapacity>
esp_err_t HttpServer<BodyCapacity>::stop() noexcept {
    if (handle_ == nullptr) {
        return ESP_OK;
    }
    httpd_handle_t handle = handle_;
    handle_ = nullptr;
    const esp_err_t err = httpd_stop(handle);
    destroy_context();
    return err;
}

// --- Request handling
// ---------------------------------------------------------

template<std::size_t BodyCapacity>
esp_err_t HttpServer<BodyCapacity>::send_error(httpd_req_t* req,
                                               HttpStatus status) noexcept {
    // Generated errors carry the status text as their body, so a client (or
    // `curl`) shows something useful.
    const char* reason = to_reason(status);
    return send_response(req, status, content_type_,
                         std::string_view(reason, std::strlen(reason)));
}

template<std::size_t BodyCapacity>
esp_err_t HttpServer<BodyCapacity>::handle_request(httpd_req_t* req) noexcept {
    auto* self = static_cast<HttpServer*>(req->user_ctx);
    if (self == nullptr || self->context_ == nullptr) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                   "server not ready");
    }
    return self->serve(req);
}

template<std::size_t BodyCapacity>
esp_err_t HttpServer<BodyCapacity>::serve(httpd_req_t* req) noexcept {
    RequestContext& ctx = *context_;
    ctx.req = req;

    // --- Method -------------------------------------------------------------
    HttpMethod method;
    if (req->method == kMethodGet) {
        method = HttpMethod::kGet;
    } else if (req->method == kMethodPost) {
        method = HttpMethod::kPost;
    } else {
        return send_error(req, HttpStatus::kMethodNotAllowed);
    }

    // --- URI -> path --------------------------------------------------------
    const std::optional<std::size_t> path_length =
        normalize_url(req->uri, ctx.path);
    if (!path_length) {
        return send_error(req, HttpStatus::kUriTooLong);
    }
    const std::string_view path(ctx.path.data(), *path_length);

    // --- Route --------------------------------------------------------------
    HttpHandler<BodyCapacity> handler = nullptr;
    for (const Route& route : routes_) {
        if (route.handler != nullptr &&
            matches(std::string_view(route.prefix.data(), route.prefix.size()),
                    path)) {
            handler = route.handler;
            break;
        }
    }
    if (handler == nullptr) {
        return send_error(req, HttpStatus::kNotFound);
    }

    // --- Body ---------------------------------------------------------------
    // Reject an oversized body before reading a single byte of it.
    const std::size_t body_limit = ctx.body.size();
    if (req->content_len > body_limit) {
        ESP_LOGW(kTag, "request body of %u bytes exceeds %u",
                 static_cast<unsigned>(req->content_len),
                 static_cast<unsigned>(body_limit));
        return send_error(req, HttpStatus::kPayloadTooLarge);
    }
    std::size_t received = 0;
    char* body_data = reinterpret_cast<char*>(ctx.body.data());
    while (received < req->content_len) {
        const int chunk = httpd_req_recv(req, body_data + received,
                                         req->content_len - received);
        if (chunk == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;  // Keep waiting for the rest of the body.
        }
        if (chunk <= 0) {
            ESP_LOGW(kTag, "failed to receive request body");
            return send_error(req, HttpStatus::kBadRequest);
        }
        received += static_cast<std::size_t>(chunk);
    }
    const std::span<const std::byte> body(ctx.body.data(), received);

    // --- Fields -------------------------------------------------------------
    HttpFields& fields = ctx.fields();
    fields.clear();
    bool truncated = false;
    if (method == HttpMethod::kGet) {
        // The query string is part of the URI.
        const char* question = std::strchr(req->uri, '?');
        if (question != nullptr) {
            const char* hash = std::strchr(question, '#');
            const char* query_end =
                hash != nullptr ? hash : question + std::strlen(question);
            const std::string_view query(
                question + 1,
                static_cast<std::size_t>(query_end - (question + 1)));
            if (!parse_form(query, /*plus_as_space=*/false, fields,
                            truncated)) {
                return send_error(req, HttpStatus::kBadRequest);
            }
        }
    } else if (received > 0) {
        std::array<char, kMaxContentTypeHeader> header{};
        const std::size_t header_length =
            httpd_req_get_hdr_value_len(req, "Content-Type");
        if (header_length > 0) {
            // A header that does not fit counts as not-a-form (it certainly
            // is not `kFormContentType`): the request is rejected, not
            // silently parsed.
            if (header_length >= header.size() ||
                httpd_req_get_hdr_value_str(req, "Content-Type", header.data(),
                                            header.size()) != ESP_OK) {
                return send_error(req, HttpStatus::kUnsupportedMediaType);
            }
            if (!is_form_content_type(
                    std::string_view(header.data(), header_length))) {
                // Only URL-encoded forms carry name/value pairs.
                return send_error(req, HttpStatus::kUnsupportedMediaType);
            }
        }
        if (!parse_form(std::string_view(body_data, received),
                        /*plus_as_space=*/true, fields, truncated)) {
            return send_error(req, HttpStatus::kBadRequest);
        }
    }

    // --- Dispatch -----------------------------------------------------------
    const HttpRequest request{
        .method = method,
        .uri = path,
        .body = body,
        .fields = fields,
        .fields_truncated = truncated,
    };
    ctx.response.clear();
    HttpHandlerContext<BodyCapacity> handler_ctx{request, ctx.response};
    const HttpResponse response = handler(handler_ctx);

    // --- Send ---------------------------------------------------------------
    const std::string_view content_type =
        response.content_type.empty() ? content_type_ : response.content_type;
    if (ctx.response.empty() && to_code(response.status) >= 400) {
        // An error without a body still gets the status text.
        const char* reason = to_reason(response.status);
        return send_response(req, response.status, content_type,
                             std::string_view(reason, std::strlen(reason)));
    }
    return send_response(
        req, response.status, content_type,
        std::string_view(ctx.response.data(), ctx.response.size()));
}

// Explicit instantiations for the body capacities in use (the firmware's and
// the tests'); the implementation lives in this translation unit.
template class HttpServer<kHttpSmallBody>;
template class HttpServer<kHttpBodyBytes>;

}  // namespace thermo::espidf_cpp
