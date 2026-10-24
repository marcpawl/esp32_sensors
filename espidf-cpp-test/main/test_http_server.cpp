// Tests for `espidf_cpp::HttpServer<BodyCapacity>` over a real socket.
//
// The server runs on the ESP-IDF `linux` target exactly as it does on the
// chip: a real `esp_http_server` instance, a real TCP listener on the loopback
// interface, and a raw POSIX socket in this test acting as the client. The
// cases below therefore check the contract end to end — prefix routing,
// register/unregister bookkeeping, GET query fields, POST form fields, and the
// "bad input is an HTTP error, never an abort" guarantees:
//
//   * 404 for an unknown path, 405 for a method other than GET/POST,
//   * 400 for a malformed query string or form body,
//   * 413 before a body over `kMaxHttpBodyBytes` is read,
//   * 414 for a path over `kMaxHttpUrl`,
//   * 415 for a non-form POST body,
//   * and the server still serves after a request no HTTP parser accepts.
//
// One server instance is shared by the socket cases (Unity has no cross-case
// hook that suits a listener); each starts from `prepare()`, which stops the
// server, clears every route, and starts it again on a fresh port.
// NOLINTBEGIN
#pragma GCC diagnostic push  // NOLINT(ignored diagnostics)
#pragma GCC diagnostic pop
// NOLINTEND

#include <array>
#include <string>
#include <string_view>

#include <cctype>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include "espidf_cpp/http_server.hpp"
#include "unity.h"

using namespace thermo::espidf_cpp;

namespace {

constexpr std::size_t kBody = kHttpSmallBody;
using Server = HttpServer<kBody>;

// --- Handlers ---------------------------------------------------------------
//
// A handler is a plain function pointer with no captures, so the cases below
// share these fixed handlers and identify themselves through the request (the
// path, or a query/form field).

// Echoes the method, the path, the field count, the truncation flag, and the
// body, as "METHOD path n=<fields> t=<0|1> b=<body>".
HttpResponse echo(HttpHandlerContext<kBody>& ctx) {
    const HttpRequest& request = ctx.request;
    ctx.body.append(to_string(request.method));
    ctx.body.push_back(' ');
    ctx.body.append(request.uri.data(), request.uri.size());
    ctx.body.append(" n=");
    ctx.body.push_back(request.fields.full()
                           ? 'F'
                           : static_cast<char>('0' + request.fields.size()));
    ctx.body.append(request.fields_truncated ? " t=1" : " t=0");
    ctx.body.append(" b=");
    const auto* bytes = reinterpret_cast<const char*>(request.body.data());
    ctx.body.append(bytes, request.body.size());
    return {.status = HttpStatus::kOk, .content_type = "text/plain"};
}

// Reports the first field named "x" (or "-") and the whole field list as
// "x=<first>;name=value&name=value": enough to check decoding, ordering, and
// how '+' is treated.
HttpResponse fields(HttpHandlerContext<kBody>& ctx) {
    const auto x = ctx.request.find_field("x");
    ctx.body.append("x=");
    if (x) {
        ctx.body.append(x->data(), x->size());
    } else {
        ctx.body.push_back('-');
    }
    ctx.body.append(";");
    bool first = true;
    for (const HttpField& field : ctx.request.fields) {
        if (!first) {
            ctx.body.push_back('&');
        }
        first = false;
        ctx.body.append(field.name.data(), field.name.size());
        ctx.body.push_back('=');
        ctx.body.append(field.value.data(), field.value.size());
    }
    return {.status = HttpStatus::kOk, .content_type = "text/plain"};
}

// Always answers with a server-side failure and no body: the server must send
// the status text as the body instead of an empty response.
HttpResponse unavailable(HttpHandlerContext<kBody>&) {
    return {.status = HttpStatus::kServiceUnavailable};
}

// Answers with JSON, to check a handler-chosen content type.
HttpResponse json(HttpHandlerContext<kBody>& ctx) {
    ctx.body.append("{\"ok\":true}");
    return {.status = HttpStatus::kOk, .content_type = "application/json"};
}

// Fills the response buffer exactly and checks that the non-aborting appends
// are all-or-nothing: a slice that does not fit is refused whole, and the
// buffer is never overrun. `body.capacity()` (the buffer handed to handlers) is
// the limit.
HttpResponse fill(HttpHandlerContext<kBody>& ctx) {
    const std::size_t capacity = ctx.body.capacity();
    // Fill to exactly `capacity()` characters.
    while (ctx.body.try_push_back('x')) {
    }
    const bool full = ctx.body.size() == capacity;
    // Now every append must be refused, and refuse without changing anything.
    const bool push_refused = !ctx.body.try_push_back('y');
    const bool slice_refused = !ctx.body.try_append("yz", 2);
    const bool intact = ctx.body.size() == capacity;

    ctx.body.clear();
    ctx.body.append(full && push_refused && slice_refused && intact
                        ? "full-ok"
                        : "full-failed");
    return {.status = HttpStatus::kOk};
}

// --- Client -----------------------------------------------------------------

struct RawResponse {
    int status = 0;
    std::string_view headers{};
    std::string_view body{};
};

// Sends `request` to 127.0.0.1:`port`, half-closes, and reads the whole
// response into `storage`. Returns false if the connection failed or the reply
// was not an HTTP response. The response views point into `storage`.
bool exchange(std::uint16_t port, std::string_view request,
              std::string& storage, RawResponse& response) {
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return false;
    }
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    bool ok = ::connect(fd, reinterpret_cast<sockaddr*>(&address),
                        sizeof(address)) == 0;
    std::size_t sent = 0;
    while (ok && sent < request.size()) {
        const ssize_t chunk = ::send(fd, request.data() + sent,
                                     request.size() - sent, MSG_NOSIGNAL);
        if (chunk <= 0) {
            ok = false;
        } else {
            sent += static_cast<std::size_t>(chunk);
        }
    }
    if (ok) {
        ::shutdown(fd, SHUT_WR);
    }

    storage.clear();
    std::array<char, 512> buffer{};
    while (ok) {
        const ssize_t received = ::recv(fd, buffer.data(), buffer.size(), 0);
        if (received < 0 && errno == EINTR) {
            continue;
        }
        if (received <= 0) {
            break;
        }
        storage.append(buffer.data(), static_cast<std::size_t>(received));
    }
    ::close(fd);

    const std::size_t header_end = storage.find("\r\n\r\n");
    const std::size_t code_pos = storage.find(' ');
    if (header_end == std::string::npos || code_pos == std::string::npos ||
        code_pos + 4 > storage.size()) {
        return false;
    }
    response.headers = std::string_view(storage).substr(0, header_end);
    response.body =
        std::string_view(storage).substr(header_end + std::strlen("\r\n\r\n"));
    response.status = std::stoi(storage.substr(code_pos + 1, 3));
    return true;
}

// True when `headers` contains `needle`, case-insensitively.
bool has_header(std::string_view headers, std::string_view needle) {
    std::string lowered(headers);
    std::string lowered_needle(needle);
    for (char& c : lowered) {
        c = static_cast<char>(std::tolower(c));
    }
    for (char& c : lowered_needle) {
        c = static_cast<char>(std::tolower(c));
    }
    return lowered.find(lowered_needle) != std::string::npos;
}

// --- Fixture ----------------------------------------------------------------

Server& server() {
    static Server instance;
    return instance;
}

// Stops the shared server, unregisters every route, applies `configure()`, and
// starts the server on a fresh port that this case owns. Returns the port.
template<typename Configure>
std::uint16_t prepare(Configure configure) {
    Server& srv = server();
    srv.stop();
    if (srv.route_count() > 0) {
        TEST_ASSERT_EQUAL(ESP_OK, srv.unregister_route(""));
    }
    TEST_ASSERT_EQUAL_UINT(0, srv.route_count());
    configure(srv);

    // A fresh port per case keeps a connection lingering from the previous
    // case from being mistaken for this case's request.
    static std::uint16_t port = 18080;
    port = static_cast<std::uint16_t>(port + 1);
    srv.set_port(port);
    TEST_ASSERT_EQUAL(ESP_OK, srv.start());
    return port;
}

// Sends `request` to `port` and asserts an HTTP response came back.
void round_trip(std::uint16_t port, std::string_view request,
                std::string& storage, RawResponse& response) {
    TEST_ASSERT_TRUE_MESSAGE(exchange(port, request, storage, response),
                             "no HTTP response");
}

}  // namespace

// --- Registration bookkeeping -----------------------------------------------

TEST_CASE("http_server: registering routes counts them", "[http_server]") {
    Server srv;
    TEST_ASSERT_EQUAL_UINT(0, srv.route_count());
    TEST_ASSERT_EQUAL(ESP_OK, srv.register_route("/a", echo));
    TEST_ASSERT_EQUAL(ESP_OK, srv.register_route("/b", echo));
    TEST_ASSERT_EQUAL_UINT(2, srv.route_count());
}

TEST_CASE("http_server: registering a prefix twice replaces its handler",
          "[http_server]") {
    Server srv;
    TEST_ASSERT_EQUAL(ESP_OK, srv.register_route("/a", echo));
    TEST_ASSERT_EQUAL(ESP_OK, srv.register_route("/a", json));
    TEST_ASSERT_EQUAL_UINT(1, srv.route_count());
}

TEST_CASE("http_server: prefixes are normalized", "[http_server]") {
    Server srv;
    TEST_ASSERT_EQUAL(ESP_OK, srv.register_route("a", echo));
    TEST_ASSERT_EQUAL(ESP_OK, srv.register_route("/a/", json));  // Same route.
    TEST_ASSERT_EQUAL(ESP_OK, srv.register_route("/a?x=1", echo));  // Ditto.
    TEST_ASSERT_EQUAL_UINT(1, srv.route_count());
}

TEST_CASE("http_server: registration rejects bad arguments", "[http_server]") {
    Server srv;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, srv.register_route("/a", nullptr));

    // The normalized prefix must fit kMaxHttpUrl characters (the leading
    // slash included): 63 characters + the slash fits, 64 + the slash does not.
    const std::string at_limit(kMaxHttpUrl - 1, 'p');
    TEST_ASSERT_EQUAL(ESP_OK, srv.register_route(at_limit, echo));
    TEST_ASSERT_EQUAL_UINT(1, srv.route_count());

    const std::string over_limit(kMaxHttpUrl, 'q');
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,
                      srv.register_route(over_limit, echo));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG,
                      srv.register_route("/" + over_limit, echo));
    TEST_ASSERT_EQUAL_UINT(1, srv.route_count());
}

TEST_CASE("http_server: registration is refused while running",
          "[http_server]") {
    const std::uint16_t port = prepare([](Server& srv) {
        TEST_ASSERT_EQUAL(ESP_OK, srv.register_route("/a", echo));
    });
    TEST_ASSERT_NOT_EQUAL_UINT(0, port);
    TEST_ASSERT_TRUE(server().running());
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE,
                      server().register_route("/b", echo));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, server().unregister_route("/a"));

    // Refused, not applied: the running server still has its one route, which
    // the next case's prepare() then clears.
    TEST_ASSERT_EQUAL_UINT(1, server().route_count());
    TEST_ASSERT_EQUAL(ESP_OK, server().stop());
}

TEST_CASE("http_server: unregister removes the prefix and its children",
          "[http_server]") {
    Server srv;
    TEST_ASSERT_EQUAL(ESP_OK, srv.register_route("/sim", echo));
    TEST_ASSERT_EQUAL(ESP_OK, srv.register_route("/sim/reset", echo));
    TEST_ASSERT_EQUAL(ESP_OK, srv.register_route("/simulator", echo));

    TEST_ASSERT_EQUAL(ESP_OK, srv.unregister_route("/sim"));
    TEST_ASSERT_EQUAL_UINT(1, srv.route_count());  // "/simulator" survives.
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FOUND, srv.unregister_route("/sim"));
}

TEST_CASE("http_server: unregistering below a prefix keeps the parent",
          "[http_server]") {
    Server srv;
    TEST_ASSERT_EQUAL(ESP_OK, srv.register_route("/sim", echo));
    TEST_ASSERT_EQUAL(ESP_OK, srv.register_route("/sim/reset", echo));

    // "/sim" still serves other paths, so it is not removed from below.
    TEST_ASSERT_EQUAL(ESP_OK, srv.unregister_route("/sim/reset"));
    TEST_ASSERT_EQUAL_UINT(1, srv.route_count());
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FOUND, srv.unregister_route("/sim/reset"));
}

TEST_CASE("http_server: unregistering the root removes every route",
          "[http_server]") {
    Server srv;
    TEST_ASSERT_EQUAL(ESP_OK, srv.register_route("/a", echo));
    TEST_ASSERT_EQUAL(ESP_OK, srv.register_route("/b/c", echo));

    TEST_ASSERT_EQUAL(ESP_OK, srv.unregister_route(""));
    TEST_ASSERT_EQUAL_UINT(0, srv.route_count());
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FOUND, srv.unregister_route(""));
}

TEST_CASE("http_server: a stopped server can be started again",
          "[http_server]") {
    // Every server used to share ESP-IDF's one default control-socket port,
    // so a restart could fail with EADDRINUSE.
    Server srv;
    TEST_ASSERT_EQUAL(ESP_OK, srv.register_route("/a", echo));
    srv.set_port(18601);
    TEST_ASSERT_EQUAL(ESP_OK, srv.start());
    TEST_ASSERT_EQUAL(ESP_OK, srv.stop());
    TEST_ASSERT_EQUAL(ESP_OK, srv.start());
    TEST_ASSERT_TRUE(srv.running());
    TEST_ASSERT_EQUAL(ESP_OK, srv.stop());
}

TEST_CASE("http_server: routes are kept across stop and start",
          "[http_server]") {
    Server srv;
    TEST_ASSERT_EQUAL(ESP_OK, srv.register_route("/a", echo));
    TEST_ASSERT_EQUAL(ESP_OK, srv.start());
    TEST_ASSERT_EQUAL(ESP_OK, srv.start());  // Idempotent.
    TEST_ASSERT_TRUE(srv.running());
    TEST_ASSERT_EQUAL(ESP_OK, srv.stop());
    TEST_ASSERT_FALSE(srv.running());
    TEST_ASSERT_EQUAL(ESP_OK, srv.stop());  // Already stopped.
    TEST_ASSERT_EQUAL_UINT(1, srv.route_count());
}

TEST_CASE("http_server: start without routes fails", "[http_server]") {
    Server srv;
    srv.set_port(19000);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, srv.start());
    TEST_ASSERT_FALSE(srv.running());
}

// --- Routing ----------------------------------------------------------------

TEST_CASE("http_server: a prefix route serves its paths", "[http_server]") {
    std::string storage;
    RawResponse response;
    const std::uint16_t port = prepare([](Server& srv) {
        TEST_ASSERT_EQUAL(ESP_OK, srv.register_route("/sim", echo));
        TEST_ASSERT_EQUAL(ESP_OK, srv.register_route("/simulator", json));
    });

    round_trip(port, "GET /sim HTTP/1.1\r\nHost: t\r\n\r\n", storage, response);
    TEST_ASSERT_EQUAL_INT(200, response.status);
    TEST_ASSERT_EQUAL_STRING("GET /sim n=0 t=0 b=",
                             std::string(response.body).c_str());

    round_trip(port, "GET /sim/reset HTTP/1.1\r\nHost: t\r\n\r\n", storage,
               response);
    TEST_ASSERT_EQUAL_INT(200, response.status);
    TEST_ASSERT_EQUAL_STRING("GET /sim/reset n=0 t=0 b=",
                             std::string(response.body).c_str());

    // "/simulator" is a sibling, not a child: its own handler must run (and
    // the earlier "/sim" route must not shadow it).
    round_trip(port, "GET /simulator HTTP/1.1\r\nHost: t\r\n\r\n", storage,
               response);
    TEST_ASSERT_EQUAL_INT(200, response.status);
    TEST_ASSERT_EQUAL_STRING("{\"ok\":true}",
                             std::string(response.body).c_str());
    TEST_ASSERT_TRUE(has_header(response.headers, "application/json"));
}

TEST_CASE("http_server: an unknown path is 404", "[http_server]") {
    std::string storage;
    RawResponse response;
    const std::uint16_t port = prepare([](Server& srv) {
        TEST_ASSERT_EQUAL(ESP_OK, srv.register_route("/sim", echo));
    });
    round_trip(port, "GET /other HTTP/1.1\r\nHost: t\r\n\r\n", storage,
               response);
    TEST_ASSERT_EQUAL_INT(404, response.status);
    TEST_ASSERT_EQUAL_STRING("Not Found", std::string(response.body).c_str());
}

TEST_CASE("http_server: an unsupported method is 405", "[http_server]") {
    std::string storage;
    RawResponse response;
    const std::uint16_t port = prepare([](Server& srv) {
        TEST_ASSERT_EQUAL(ESP_OK, srv.register_route("/", echo));
    });
    round_trip(port, "DELETE /sim HTTP/1.1\r\nHost: t\r\n\r\n", storage,
               response);
    TEST_ASSERT_EQUAL_INT(405, response.status);
}

TEST_CASE("http_server: the root route covers every path", "[http_server]") {
    std::string storage;
    RawResponse response;
    const std::uint16_t port = prepare([](Server& srv) {
        TEST_ASSERT_EQUAL(ESP_OK, srv.register_route("/", echo));
    });
    round_trip(port, "GET /deep/deeper HTTP/1.1\r\nHost: t\r\n\r\n", storage,
               response);
    TEST_ASSERT_EQUAL_INT(200, response.status);
    TEST_ASSERT_EQUAL_STRING("GET /deep/deeper n=0 t=0 b=",
                             std::string(response.body).c_str());
}

TEST_CASE("http_server: a route unregistered while stopped is not served",
          "[http_server]") {
    std::string storage;
    RawResponse response;
    const std::uint16_t port = prepare([](Server& srv) {
        TEST_ASSERT_EQUAL(ESP_OK, srv.register_route("/gone", echo));
        TEST_ASSERT_EQUAL(ESP_OK, srv.register_route("/kept", json));
    });
    server().stop();
    TEST_ASSERT_EQUAL(ESP_OK, server().unregister_route("/gone"));
    server().set_port(port);
    TEST_ASSERT_EQUAL(ESP_OK, server().start());

    round_trip(port, "GET /gone HTTP/1.1\r\nHost: t\r\n\r\n", storage,
               response);
    TEST_ASSERT_EQUAL_INT(404, response.status);
    round_trip(port, "GET /kept HTTP/1.1\r\nHost: t\r\n\r\n", storage,
               response);
    TEST_ASSERT_EQUAL_INT(200, response.status);
}

// --- GET fields -------------------------------------------------------------

TEST_CASE("http_server: GET fields come from the query string",
          "[http_server]") {
    std::string storage;
    RawResponse response;
    const std::uint16_t port = prepare([](Server& srv) {
        TEST_ASSERT_EQUAL(ESP_OK, srv.register_route("/f", fields));
    });
    round_trip(port, "GET /f?x=he%20llo&y=a+b&z=1 HTTP/1.1\r\nHost: t\r\n\r\n",
               storage, response);
    TEST_ASSERT_EQUAL_INT(200, response.status);
    // '%20' is a space; in a query string '+' stays a literal '+'.
    TEST_ASSERT_EQUAL_STRING("x=he llo;x=he llo&y=a+b&z=1",
                             std::string(response.body).c_str());
}

TEST_CASE("http_server: GET without a query string has no fields",
          "[http_server]") {
    std::string storage;
    RawResponse response;
    const std::uint16_t port = prepare([](Server& srv) {
        TEST_ASSERT_EQUAL(ESP_OK, srv.register_route("/f", fields));
    });
    round_trip(port, "GET /f HTTP/1.1\r\nHost: t\r\n\r\n", storage, response);
    TEST_ASSERT_EQUAL_INT(200, response.status);
    TEST_ASSERT_EQUAL_STRING("x=-;", std::string(response.body).c_str());
}

// --- POST fields ------------------------------------------------------------

TEST_CASE("http_server: POST form fields are decoded", "[http_server]") {
    std::string storage;
    RawResponse response;
    const std::uint16_t port = prepare([](Server& srv) {
        TEST_ASSERT_EQUAL(ESP_OK, srv.register_route("/p", fields));
    });
    const std::string body = "x=a+b%26c&y=%22q%22";
    const std::string request =
        "POST /p HTTP/1.1\r\nHost: t\r\n"
        "Content-Type: application/x-www-form-urlencoded\r\nContent-Length: " +
        std::to_string(body.size()) + "\r\n\r\n" + body;
    round_trip(port, request, storage, response);
    TEST_ASSERT_EQUAL_INT(200, response.status);
    // In a form body '+' is a space and '%26' is '&'.
    TEST_ASSERT_EQUAL_STRING("x=a b&c;x=a b&c&y=\"q\"",
                             std::string(response.body).c_str());
}

TEST_CASE("http_server: POST content type ignores parameters and case",
          "[http_server]") {
    std::string storage;
    RawResponse response;
    const std::uint16_t port = prepare([](Server& srv) {
        TEST_ASSERT_EQUAL(ESP_OK, srv.register_route("/p", echo));
    });
    const std::string request =
        "POST /p HTTP/1.1\r\nHost: t\r\n"
        "Content-Type: Application/X-WWW-Form-Urlencoded; charset=UTF-8\r\n"
        "Content-Length: 3\r\n\r\nx=1";
    round_trip(port, request, storage, response);
    TEST_ASSERT_EQUAL_INT(200, response.status);
    TEST_ASSERT_EQUAL_STRING("POST /p n=1 t=0 b=x=1",
                             std::string(response.body).c_str());
}

TEST_CASE("http_server: a non-form POST body is 415", "[http_server]") {
    std::string storage;
    RawResponse response;
    const std::uint16_t port = prepare([](Server& srv) {
        TEST_ASSERT_EQUAL(ESP_OK, srv.register_route("/p", echo));
    });
    round_trip(port,
               "POST /p HTTP/1.1\r\nHost: t\r\n"
               "Content-Type: application/json\r\nContent-Length: 2\r\n\r\n{}",
               storage, response);
    TEST_ASSERT_EQUAL_INT(415, response.status);
}

TEST_CASE("http_server: a malformed form escape is 400", "[http_server]") {
    std::string storage;
    RawResponse response;
    const std::uint16_t port = prepare([](Server& srv) {
        TEST_ASSERT_EQUAL(ESP_OK, srv.register_route("/p", echo));
    });
    round_trip(port,
               "POST /p HTTP/1.1\r\nHost: t\r\n"
               "Content-Type: application/x-www-form-urlencoded\r\n"
               "Content-Length: 6\r\n\r\nx=%zz1",
               storage, response);
    TEST_ASSERT_EQUAL_INT(400, response.status);
}

TEST_CASE("http_server: an oversized POST body is 413", "[http_server]") {
    std::string storage;
    RawResponse response;
    const std::uint16_t port = prepare([](Server& srv) {
        TEST_ASSERT_EQUAL(ESP_OK, srv.register_route("/p", echo));
    });
    const std::size_t size = kMaxHttpBodyBytes + 1;
    const std::string request =
        "POST /p HTTP/1.1\r\nHost: t\r\n"
        "Content-Type: application/x-www-form-urlencoded\r\nContent-Length: " +
        std::to_string(size) + "\r\n\r\n" + std::string(size, 'a');
    round_trip(port, request, storage, response);
    TEST_ASSERT_EQUAL_INT(413, response.status);
}

// --- Limits ----------------------------------------------------------------

TEST_CASE("http_server: an over-long path is 414", "[http_server]") {
    std::string storage;
    RawResponse response;
    const std::uint16_t port = prepare([](Server& srv) {
        TEST_ASSERT_EQUAL(ESP_OK, srv.register_route("/", echo));
    });
    const std::string path = "/" + std::string(kMaxHttpUrl, 'p');
    round_trip(port, "GET " + path + " HTTP/1.1\r\nHost: t\r\n\r\n", storage,
               response);
    TEST_ASSERT_EQUAL_INT(414, response.status);
}

TEST_CASE("http_server: too many fields are reported, not fatal",
          "[http_server]") {
    std::string storage;
    RawResponse response;
    const std::uint16_t port = prepare([](Server& srv) {
        TEST_ASSERT_EQUAL(ESP_OK, srv.register_route("/f", echo));
    });
    // More fields than the request can hold, each one well within limits.
    std::string query;
    for (std::size_t i = 0; i < kMaxHttpFormFields + 2; ++i) {
        if (i > 0) {
            query.push_back('&');
        }
        query += "f" + std::to_string(i) + "=v";
    }
    round_trip(port, "GET /f?" + query + " HTTP/1.1\r\nHost: t\r\n\r\n",
               storage, response);
    TEST_ASSERT_EQUAL_INT(200, response.status);
    // All 16 slots are in use ("n=F") and the extra fields were reported.
    TEST_ASSERT_EQUAL_STRING("GET /f n=F t=1 b=",
                             std::string(response.body).c_str());
}

TEST_CASE("http_server: an over-long field value is dropped, not fatal",
          "[http_server]") {
    std::string storage;
    RawResponse response;
    const std::uint16_t port = prepare([](Server& srv) {
        TEST_ASSERT_EQUAL(ESP_OK, srv.register_route("/f", echo));
    });
    round_trip(port,
               "GET /f?x=" + std::string(kMaxHttpFieldValue + 1, 'v') +
                   "&y=1 HTTP/1.1\r\nHost: t\r\n\r\n",
               storage, response);
    TEST_ASSERT_EQUAL_INT(200, response.status);
    TEST_ASSERT_EQUAL_STRING("GET /f n=1 t=1 b=",
                             std::string(response.body).c_str());
}

// --- Responses --------------------------------------------------------------

TEST_CASE("http_server: a handler-chosen error gets the status text",
          "[http_server]") {
    std::string storage;
    RawResponse response;
    const std::uint16_t port = prepare([](Server& srv) {
        TEST_ASSERT_EQUAL(ESP_OK, srv.register_route("/u", unavailable));
    });
    round_trip(port, "GET /u HTTP/1.1\r\nHost: t\r\n\r\n", storage, response);
    TEST_ASSERT_EQUAL_INT(503, response.status);
    TEST_ASSERT_EQUAL_STRING("Service Unavailable",
                             std::string(response.body).c_str());
}

TEST_CASE("http_server: a handler can fill its response buffer exactly",
          "[http_server]") {
    std::string storage;
    RawResponse response;
    const std::uint16_t port = prepare([](Server& srv) {
        TEST_ASSERT_EQUAL(ESP_OK, srv.register_route("/fill", fill));
    });
    round_trip(port, "GET /fill HTTP/1.1\r\nHost: t\r\n\r\n", storage,
               response);
    TEST_ASSERT_EQUAL_INT(200, response.status);

    TEST_ASSERT_EQUAL_STRING("full-ok", std::string(response.body).c_str());
}

// --- Bad requests
// -------------------------------------------------------------

TEST_CASE("http_server: garbage in is an HTTP error, not a crash",
          "[http_server]") {
    std::string storage;
    RawResponse response;
    const std::uint16_t port = prepare([](Server& srv) {
        TEST_ASSERT_EQUAL(ESP_OK, srv.register_route("/", echo));
    });

    // Either an HTTP error or a closed connection is acceptable — what matters
    // is that the server survives and answers the next request.
    const std::string_view garbage[] = {
        "not http at all\r\n\r\n",
        "GET\r\n\r\n",
        "GET  HTTP/1.1\r\n\r\n",
        "GET /x HTTP/9.9\r\nHost: t\r\n\r\n",
        "\r\n\r\n",
    };
    for (const std::string_view request : garbage) {
        if (exchange(port, request, storage, response)) {
            // Any 4xx/5xx is fine (e.g. 400 for a bad request line, or 505
            // when the HTTP version is unacceptable).
            TEST_ASSERT_GREATER_OR_EQUAL_INT(400, response.status);
        }
    }

    round_trip(port, "GET /after HTTP/1.1\r\nHost: t\r\n\r\n", storage,
               response);
    TEST_ASSERT_EQUAL_INT(200, response.status);
    TEST_ASSERT_EQUAL_STRING("GET /after n=0 t=0 b=",
                             std::string(response.body).c_str());
}
