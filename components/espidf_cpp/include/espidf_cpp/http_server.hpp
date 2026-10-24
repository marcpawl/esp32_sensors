// HttpServer<BodyCapacity> — a small wrapper over ESP-IDF's `esp_http_server`
// that serves *prefix* routes with one GET/POST handler each.
//
// What it adds over registering `httpd_uri_t` handlers by hand:
//
//   * **Prefix routes.** `register_route("/simulator", handler)` serves the
//     path "/simulator" and every path below it ("/simulator/reset"), without
//     a handler (and a compiled-in route table) per URI. `unregister_route()`
//     removes a prefix again — at run time, while the server is stopped.
//
//   * **One handler for GET and POST.** The handler is called with the method
//     (`HttpMethod::kGet` / `HttpMethod::kPost`) and the submitted name/value
//     pairs, already parsed and URL-decoded — from the query string for GET and
//     from a form-encoded body for POST.
//
//   * **Bad input is an HTTP error, never an abort.** A body over the limit, a
//     malformed body or URI, an unknown route, or an unsupported method are
//     answered with 413 / 400 / 414 / 404 / 405. Nothing in the request path
//     asserts or aborts.
//
//   * **No dynamic memory in the request path.** The request body, the decoded
//     fields, and the response body all live in one request context that is
//     allocated once, when the server starts; handling a request performs no
//     allocation.
//
// Handler contract
// ----------------
// A handler is a plain function pointer, `HttpHandler<BodyCapacity>`:
//
//     HttpResponse simulator(HttpHandlerContext<kBody>& ctx) {
//         ctx.body.append("{\"ok\":true}");
//         return {.status = HttpStatus::kOk,
//                 .content_type = "application/json"};
//     }
//
// It writes the response body into `ctx.body` (empty on entry, `capacity()`
// characters available) and returns the status and content type. A handler
// that cannot fit its body returns a 5xx status instead. `ctx.request` borrows
// the request storage and is valid only for the duration of the call.
//
// Lifetime
// --------
// `register_route()` / `unregister_route()` may only be called while the server
// is stopped; `start()` installs all registered routes and serves until
// `stop()`. The registered routes survive a stop.
//
// `BodyCapacity` must be one of the instantiations compiled into
// `http_server.cpp` (`kHttpSmallBody`, `kHttpBodyBytes`): the implementation
// lives there, not in this header, so the ESP-IDF server code is compiled once.
// Choose the closest existing capacity (or add another instantiation at the
// bottom of `http_server.cpp`).
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>

#include "esp_err.h"
#include "esp_http_server.h"

#include "espidf_cpp/http_server_types.hpp"
#include "espidf_cpp/static_string.hpp"

namespace thermo::espidf_cpp {

// The HTTP server.
//
// `BodyCapacity` is the size of the response-body buffer handed to handlers
// (so the largest response body they can build). Request bodies are capped
// separately at `kMaxHttpBodyBytes`.
//
// ESP-IDF runs one HTTP task per server and handles requests one at a time, so
// one request context per server is shared. The server object is neither
// copyable nor movable.
template<std::size_t BodyCapacity>
class HttpServer {
  public:
    HttpServer() noexcept = default;
    ~HttpServer();

    HttpServer(HttpServer const&) = delete;
    HttpServer(HttpServer&&) = delete;
    HttpServer& operator=(HttpServer const&) = delete;
    HttpServer& operator=(HttpServer&&) = delete;

    // Registers `handler` for `prefix` ("/simulator") and every path below it.
    //
    // The prefix is normalized like a request path (see `normalize_url()`):
    // "/simulator", "/simulator/" and "simulator" are the same prefix, and
    // "" and "/" both mean the root, which matches every request (register it
    // last if used). Registering an already-registered prefix replaces its
    // handler.
    //
    // Returns:
    //   ESP_OK                — registered;
    //   ESP_ERR_INVALID_STATE — the server is running (stop() it first);
    //   ESP_ERR_INVALID_ARG   — `handler` is null, or the prefix does not fit
    //                           `kMaxHttpUrl`;
    //   ESP_ERR_NO_MEM        — `kMaxHttpRoutes` routes are already registered.
    esp_err_t register_route(std::string_view prefix,
                             HttpHandler<BodyCapacity> handler) noexcept;

    // Removes the route for `prefix` and every route below it ("/sim" removes
    // "/sim" and "/sim/*", but not "/simulator"). Unregistering a nested
    // prefix does NOT remove its parent: "/sim/reset" removes a route
    // registered as "/sim/reset" while "/sim" stays registered, because "/sim"
    // still serves other paths. The root ("" or "/") removes every route.
    //
    // Returns ESP_OK if something was removed, ESP_ERR_NOT_FOUND if there was
    // nothing to remove, and ESP_ERR_INVALID_STATE while the server is running
    // (stop it first; the removal takes effect when it is restarted).
    esp_err_t unregister_route(std::string_view prefix) noexcept;

    // Number of registered routes.
    [[nodiscard]] std::size_t route_count() const noexcept { return count_; }

    // Listening port (default 80).
    void set_port(std::uint16_t port) noexcept { port_ = port; }

    // Content type sent for responses that do not set one (default
    // "text/plain"). Not copied, so pass a string literal.
    void set_content_type(std::string_view content_type) noexcept {
        content_type_ = content_type;
    }

    // Starts listening; requires at least one route. The request context is
    // allocated here, once. Idempotent: starting a running server returns
    // ESP_OK.
    esp_err_t start() noexcept;

    // Stops serving. Registered routes are kept, so the server can be started
    // again. Succeeds when the server was not running.
    esp_err_t stop() noexcept;

    [[nodiscard]] bool running() const noexcept { return handle_ != nullptr; }

  private:
    // The response content type, copied from the handler's response so the
    // NUL-terminated form outlives the handler call (httpd_resp_set_type()
    // does not copy its argument either, but keeps the pointer until the
    // response is written, which happens after this member is set).
    using ContentType = static_string<64>;

    // The decoded fields of one request (the vector plus its inline
    // name/value storage), constructed in place in suitably aligned raw
    // storage so RequestContext does not construct it twice.
    static constexpr std::size_t kFieldsBytes =
        (sizeof(HttpFields) + alignof(HttpField) - 1) / alignof(HttpField) *
        alignof(HttpField);

    struct FieldsStorage {
        alignas(HttpField) char bytes[kFieldsBytes];
    };

    // Everything one request needs: the normalized path, the decoded fields,
    // the raw body buffer, and the response buffer. Constructed once by
    // start() and reused for every request (the server handles one request at
    // a time), so request handling never allocates.
    struct alignas(HttpField) RequestContext {
        // Constructs the field vector in `fields_storage`; destroy_context()
        // destroys it.
        RequestContext();

        std::array<char, kMaxHttpUrl> path{};
        FieldsStorage fields_storage{};
        std::array<std::byte, kMaxHttpBodyBytes> body{};
        static_string<BodyCapacity> response{};
        httpd_req_t* req = nullptr;

        [[nodiscard]] HttpFields& fields() noexcept {
            return *std::launder(
                reinterpret_cast<HttpFields*>(&fields_storage));
        }
    };

    struct Route {
        static_string<kMaxHttpUrl> prefix;
        HttpHandler<BodyCapacity> handler = nullptr;
    };

    // True when `prefix` covers `path` ("/sim" covers "/sim" and "/sim/x",
    // but not "/simulator").
    [[nodiscard]] static bool matches(std::string_view prefix,
                                      std::string_view path) noexcept;

    // The ESP-IDF handler installed for every route: dispatch to `serve()`.
    static esp_err_t handle_request(httpd_req_t* req) noexcept;

    // Parses `req` and serves it from the route table; always sends a response.
    esp_err_t serve(httpd_req_t* req) noexcept;

    // Sends a generated error response.
    esp_err_t send_error(httpd_req_t* req, HttpStatus status) noexcept;

    // Frees the request context (must be called with the ESP-IDF server
    // stopped, so nothing is using it).
    void destroy_context() noexcept;

    std::array<Route, kMaxHttpRoutes> routes_{};
    std::size_t count_ = 0;

    httpd_handle_t handle_ = nullptr;
    std::uint16_t port_ = 80;
    std::string_view content_type_ = "text/plain";

    // Allocated by start(); freed by stop(). Only referenced while running.
    std::unique_ptr<RequestContext> context_{};
};

}  // namespace thermo::espidf_cpp
