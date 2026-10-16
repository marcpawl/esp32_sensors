// Native tests for `thermo::HttpsServer` (`espidf_cpp`) — the Configure-mode
// router (§6).
//
// The firmware serves the Configure-mode UI over TLS, but the TLS listener has
// no ESP-IDF `linux` port, so these tests construct the server with
// `Scheme::kHttp` and stand up a *real* plain-HTTP listener on the target. That
// exercises the whole router: `start()` binds and installs the routes, the
// registered handler reads the request body and returns a response, and the
// client sees the bytes on the wire.
//
// The client is deliberately hand-rolled over plain POSIX sockets rather than
// `esp_http_client`, so the tests assert on the raw HTTP exchange and pull in
// no extra component. Because the ESP-IDF HTTP server runs on its own FreeRTOS
// task, the client polls with `select` and yields between polls so that task
// gets scheduled.
#include "unity.h"

#include <cstdint>
#include <cstring>
#include <string>

#include "esp_err.h"
#include "https_server.hpp"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {

// High, fixed ports unlikely to collide with a developer's services. Linux
// needs no privilege for these. Each end-to-end test owns a distinct port so a
// failure in one cannot leave a listener bound that breaks another.
constexpr std::uint16_t kRouteTablePort = 18080;
constexpr std::uint16_t kServePort = 18081;
constexpr std::uint16_t kLifecyclePort = 18082;

// Sends a raw HTTP request to 127.0.0.1:`port` and returns everything the
// server sends back. The read is bounded by a poll deadline: the ESP-IDF HTTP
// server runs on its own FreeRTOS task, so the caller yields between polls to
// let that task accept and serve.
std::string http_round_trip(std::uint16_t port, const std::string& request) {
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    TEST_ASSERT_TRUE_MESSAGE(fd >= 0, "socket() failed");

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    const int rc =
        ::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, rc, "connect() to the test server failed");

    const ssize_t sent = ::send(fd, request.data(), request.size(), 0);
    TEST_ASSERT_EQUAL_INT_MESSAGE(static_cast<int>(request.size()),
                                  static_cast<int>(sent), "send() short write");

    std::string response;
    char buf[512];
    constexpr int kMaxPolls = 200;  // 200 * (10 ms + yield) ~= a few seconds
    for (int poll = 0; poll < kMaxPolls; ++poll) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(fd, &readfds);
        timeval tv{};
        tv.tv_usec = 10000;  // 10 ms
        const int ready = ::select(fd + 1, &readfds, nullptr, nullptr, &tv);
        if (ready > 0) {
            const ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
            if (n <= 0) {
                break;  // Peer closed: response is complete.
            }
            response.append(buf, static_cast<std::size_t>(n));
            // The handlers here have no body framing beyond the blank line.
            if (response.find("\r\n\r\n") != std::string::npos) {
                break;
            }
        }
        // Yield so the HTTP server's FreeRTOS task can run.
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    ::close(fd);
    return response;
}

// A POST handler with the shape the operator-facing pages use: read the request
// body, parse it, and return a response. It echoes the `name` field.
thermo::HttpResponse greet(thermo::HttpRequest& request) {
    thermo::HttpResponse response;
    const auto body = request.body();
    if (!body) {
        response.status = "400 Bad Request";
        response.content_type = "text/plain";
        response.body = "Bad Request";
        return response;
    }
    // Minimal form parsing: find "name=" and take the rest.
    const std::string prefix = "name=";
    const std::size_t at = body->find(prefix);
    const std::string name = (at == std::string::npos)
                                 ? std::string()
                                 : body->substr(at + prefix.size());
    response.content_type = "text/plain";
    response.body = "Hello " + name;
    return response;
}

}  // namespace

// --- Route-table semantics (no listener required) ---------------------------

TEST_CASE("espidf_cpp_test https server: duplicate route is rejected",
          "[espidf_cpp_test][https]") {
    thermo::HttpsServer server(thermo::Scheme::kHttp, kRouteTablePort);
    const auto handler = [](thermo::HttpRequest&) {
        return thermo::HttpResponse{};
    };

    TEST_ASSERT_EQUAL(ESP_OK, server.register_handler(
                                  "/a", thermo::HttpMethod::kGet, handler));
    // Same fragment + method again is a duplicate.
    TEST_ASSERT_EQUAL(
        ESP_ERR_INVALID_ARG,
        server.register_handler("/a", thermo::HttpMethod::kGet, handler));
    // Same fragment, different method is a distinct route.
    TEST_ASSERT_EQUAL(ESP_OK, server.register_handler(
                                  "/a", thermo::HttpMethod::kPost, handler));
}

TEST_CASE("espidf_cpp_test https server: deregister reports missing routes",
          "[espidf_cpp_test][https]") {
    thermo::HttpsServer server(thermo::Scheme::kHttp, kRouteTablePort);
    TEST_ASSERT_EQUAL(
        ESP_ERR_NOT_FOUND,
        server.deregister_handler("/missing", thermo::HttpMethod::kGet));
    TEST_ASSERT_EQUAL(ESP_OK,
                      server.register_handler("/a", thermo::HttpMethod::kGet,
                                              [](thermo::HttpRequest&) {
                                                  return thermo::HttpResponse{};
                                              }));
    TEST_ASSERT_EQUAL(
        ESP_OK, server.deregister_handler("/a", thermo::HttpMethod::kGet));
    // It is gone now.
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FOUND, server.deregister_handler(
                                             "/a", thermo::HttpMethod::kGet));
}

TEST_CASE("espidf_cpp_test https server: route table fills then reports no mem",
          "[espidf_cpp_test][https]") {
    thermo::HttpsServer server(thermo::Scheme::kHttp, kRouteTablePort);
    const auto handler = [](thermo::HttpRequest&) {
        return thermo::HttpResponse{};
    };

    // The table holds eight routes; the ninth is rejected.
    for (int i = 0; i < 8; ++i) {
        const std::string fragment = "/r" + std::to_string(i);
        TEST_ASSERT_EQUAL(
            ESP_OK, server.register_handler(fragment, thermo::HttpMethod::kGet,
                                            handler));
    }
    TEST_ASSERT_EQUAL(ESP_ERR_NO_MEM,
                      server.register_handler(
                          "/overflow", thermo::HttpMethod::kGet, handler));
}

// --- End-to-end over a real plain-HTTP listener -----------------------------

TEST_CASE("espidf_cpp_test https server: serves a POST body and responds",
          "[espidf_cpp_test][https]") {
    thermo::HttpsServer server(thermo::Scheme::kHttp, kServePort);
    TEST_ASSERT_EQUAL(kServePort, server.port());
    TEST_ASSERT_EQUAL(ESP_OK, server.register_handler(
                                  "/greet", thermo::HttpMethod::kPost, greet));
    TEST_ASSERT_EQUAL(ESP_OK, server.start());

    const std::string body = "name=Marc";
    const std::string request =
        "POST /greet HTTP/1.1\r\n"
        "Host: 127.0.0.1\r\n"
        "Content-Type: text/plain\r\n"
        "Content-Length: " +
        std::to_string(body.size()) +
        "\r\n"
        "Connection: close\r\n"
        "\r\n" +
        body;

    const std::string response = http_round_trip(kServePort, request);

    // The status line and the echoed body both made it across the wire.
    TEST_ASSERT_NOT_EQUAL(std::string::npos, response.find("200 OK"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, response.find("Hello Marc"));

    TEST_ASSERT_EQUAL(ESP_OK, server.stop());
}

TEST_CASE("espidf_cpp_test https server: start is idempotent and stop is safe",
          "[espidf_cpp_test][https]") {
    thermo::HttpsServer server(thermo::Scheme::kHttp, kLifecyclePort);
    TEST_ASSERT_EQUAL(ESP_OK, server.register_handler(
                                  "/greet", thermo::HttpMethod::kPost, greet));
    TEST_ASSERT_EQUAL(ESP_OK, server.start());
    // A second start is a no-op.
    TEST_ASSERT_EQUAL(ESP_OK, server.start());
    TEST_ASSERT_EQUAL(ESP_OK, server.stop());
    // Stopping an unstarted server is a no-op.
    TEST_ASSERT_EQUAL(ESP_OK, server.stop());
}
