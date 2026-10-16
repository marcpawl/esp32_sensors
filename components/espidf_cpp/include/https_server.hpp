// HTTPS configuration server (§6).
//
// The server is a thin, protocol-level router: it owns the TLS listener and a
// table of URI handlers, but knows nothing about the configuration pages. Each
// page (GET /, GET /scan, POST /save, GET /saved) is contributed by the client
// via `register_handler`, so the routing table is assembled by the caller and
// the pages can live with the rest of the Configure-mode logic.
#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "esp_err.h"

// esp_https_server.h declares `typedef struct httpd_req httpd_req_t;`. Mirror
// that here so `HttpRequest` can hold a pointer without pulling the
// chip-specific header into every translation unit that uses the router.
struct httpd_req;
using httpd_req_t = struct httpd_req;

namespace thermo {

// HTTP methods the router understands. Values are opaque; they are mapped to
// the ESP-IDF `httpd_method_t` internally.
enum class HttpMethod { kGet, kPost };

// A response the router should send back to the client.
struct HttpResponse {
    std::string status = "200 OK";
    std::string content_type = "text/html";
    std::string body;
    // Extra headers (e.g. { "Location", "/saved" } for a redirect).
    std::vector<std::pair<std::string, std::string>> headers;
};

// Read-only view of an incoming request, handed to a registered handler.
//
// Wraps the underlying `httpd_req_t` so handlers (and their tests) do not need
// to include the chip-specific `esp_https_server.h`. The request body is read
// on demand; a handler that reads it more than once gets the cached copy.
class HttpRequest {
  public:
    explicit HttpRequest(httpd_req_t* req) noexcept : req_(req) {}

    // The request method, e.g. "GET" or "POST".
    std::string method() const;

    // The request URI path, e.g. "/save".
    std::string uri() const;

    // The raw request body. Returns nullopt when it is absent or exceeds the
    // internal size cap; an empty request has a present-but-empty body.
    std::optional<std::string> body();

  private:
    httpd_req_t* req_;
    std::optional<std::string> body_;  // Cached after the first read.
    bool body_read_ = false;
};

// Callback invoked to serve one URI: receives the request and returns the
// response to send. Returning a response (rather than writing to the socket)
// keeps handlers host-testable.
using HttpHandler = std::function<HttpResponse(HttpRequest&)>;

// Serves the Configure-mode UI over TLS on port 443 (§6).
//
// Handlers are registered with `register_handler` before (or after) `start()`;
// a route registered before start is applied to the listener when it comes up.
class HttpsServer {
  public:
    HttpsServer();
    ~HttpsServer();

    HttpsServer(const HttpsServer&) = delete;
    HttpsServer& operator=(const HttpsServer&) = delete;

    // Starts the TLS listener. Routes registered before this call are applied
    // as part of the start.
    esp_err_t start();

    // Stops the listener. Registered routes are retained and are re-applied by
    // a subsequent `start()`.
    esp_err_t stop();

    // Registers the handler for `method` + `fragment`. Returns an error if the
    // route table is full or the fragment is already registered. When the
    // server is running the route is installed immediately.
    esp_err_t register_handler(const std::string& fragment, HttpMethod method,
                               HttpHandler handler);

    // Removes a previously registered route. Returns an error when no matching
    // route exists.
    esp_err_t deregister_handler(const std::string& fragment,
                                 HttpMethod method);

  private:
    // Fixed-capacity route table. It is defined in the .cpp (it owns the
    // chip-specific httpd_uri_t descriptors) and hidden here so this header
    // stays free of ESP-IDF HTTP internals. Held by pointer so its element
    // addresses - stored as `user_ctx` - are stable.
    struct RouteTable;

    std::unique_ptr<RouteTable> routes_;
    void* handle_ = nullptr;  // httpd_handle_t; null until started.
};

}  // namespace thermo
