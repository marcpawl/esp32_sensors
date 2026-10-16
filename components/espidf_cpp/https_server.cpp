// HTTPS configuration server (§6). A thin TLS router over esp_https_server.
#include "https_server.hpp"

#include <array>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "esp_https_server.h"
#include "esp_log.h"
#include "self_signed_cert.hpp"

namespace thermo {
namespace {

constexpr const char* kTag = "https";

httpd_method_t to_httpd_method(HttpMethod method) {
    switch (method) {
        case HttpMethod::kGet:
            return HTTP_GET;
        case HttpMethod::kPost:
            return HTTP_POST;
    }
    return HTTP_GET;
}

// Sends a response. Status and headers go out before the body.
esp_err_t send_response(httpd_req_t* req, const HttpResponse& response) {
    httpd_resp_set_status(req, response.status.c_str());
    httpd_resp_set_type(req, response.content_type.c_str());
    for (const auto& [name, value] : response.headers) {
        httpd_resp_set_hdr(req, name.c_str(), value.c_str());
    }
    return httpd_resp_send(req, response.body.c_str(), response.body.size());
}

}  // namespace

// One registered route: the client handler plus the ESP-IDF descriptor that
// points back at it. The descriptor's `user_ctx` is set to `this`, so the
// trampoline can recover the handler from the request.
struct Route {
    std::string fragment;
    HttpMethod method = HttpMethod::kGet;
    HttpHandler handler;
    httpd_uri_t uri = {};

    // C trampoline installed in the listener; recovers the Route from the
    // request's `user_ctx` and writes the handler's response.
    static esp_err_t dispatch(httpd_req_t* req) {
        auto* route = static_cast<Route*>(req->user_ctx);
        HttpRequest request(req);
        const HttpResponse response = route->handler(request);
        return send_response(req, response);
    }
};

// Fixed-capacity route table. A fixed array (rather than a growing vector)
// keeps every slot's address stable, since each slot's `user_ctx` points at
// its `Route`.
struct HttpsServer::RouteTable {
    static constexpr std::size_t kMaxRoutes = 8;
    std::array<std::optional<Route>, kMaxRoutes> slots;
};

std::string HttpRequest::method() const {
    switch (req_->method) {
        case HTTP_GET:
            return "GET";
        case HTTP_POST:
            return "POST";
        default:
            return "OTHER";
    }
}

std::string HttpRequest::uri() const {
    return std::string(req_->uri);
}

std::optional<std::string> HttpRequest::body() {
    if (body_read_) {
        return body_;
    }
    body_read_ = true;

    constexpr std::size_t kMaxBody = 8192;
    if (static_cast<std::size_t>(req_->content_len) > kMaxBody) {
        return std::nullopt;
    }

    std::string data(req_->content_len, '\0');
    std::size_t received = 0;
    while (received < data.size()) {
        const int ret = httpd_req_recv(req_, data.data() + received,
                                       data.size() - received);
        if (ret <= 0) {
            if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
                continue;  // Retry on timeout.
            }
            return std::nullopt;
        }
        received += static_cast<std::size_t>(ret);
    }
    body_ = std::move(data);
    return body_;
}

HttpsServer::HttpsServer() : routes_(std::make_unique<RouteTable>()) {}

HttpsServer::~HttpsServer() {
    stop();
}

esp_err_t HttpsServer::register_handler(const std::string& fragment,
                                        HttpMethod method,
                                        HttpHandler handler) {
    // Reject duplicates: the ESP-IDF listener would otherwise return an opaque
    // error and the caller could not tell which route was at fault.
    for (auto& slot : routes_->slots) {
        if (slot && slot->fragment == fragment && slot->method == method) {
            return ESP_ERR_INVALID_ARG;
        }
    }

    // Find a free slot. Slots are only ever reset in place, keeping every
    // element address stable for `user_ctx` to keep pointing at.
    std::optional<Route>* free_slot = nullptr;
    for (auto& slot : routes_->slots) {
        if (!slot) {
            free_slot = &slot;
            break;
        }
    }
    if (free_slot == nullptr) {
        return ESP_ERR_NO_MEM;
    }

    free_slot->emplace();
    Route& route = **free_slot;
    route.fragment = fragment;
    route.method = method;
    route.handler = std::move(handler);
    route.uri = {};
    route.uri.uri = route.fragment.c_str();
    route.uri.method = to_httpd_method(method);
    route.uri.handler = &Route::dispatch;
    route.uri.user_ctx = &route;

    // Only touch the live listener when it is running; otherwise start() will
    // install every pending route.
    if (handle_ == nullptr) {
        return ESP_OK;
    }
    return httpd_register_uri_handler(static_cast<httpd_handle_t>(handle_),
                                      &route.uri);
}

esp_err_t HttpsServer::deregister_handler(const std::string& fragment,
                                          HttpMethod method) {
    for (auto& slot : routes_->slots) {
        if (!slot || slot->fragment != fragment || slot->method != method) {
            continue;
        }
        if (handle_ != nullptr) {
            const esp_err_t err = httpd_unregister_uri_handler(
                static_cast<httpd_handle_t>(handle_), slot->fragment.c_str(),
                to_httpd_method(method));
            if (err != ESP_OK) {
                return err;
            }
        }
        slot.reset();
        return ESP_OK;
    }
    return ESP_ERR_NOT_FOUND;
}

esp_err_t HttpsServer::start() {
    if (handle_ != nullptr) {
        return ESP_OK;  // Already running.
    }

    httpd_ssl_config_t conf = HTTPD_SSL_CONFIG_DEFAULT();
    conf.port_secure = 443;
    // mbedTLS 4.x (shipped with ESP-IDF v6.1) requires the PEM buffer length
    // to include the terminating NUL byte; the `*_len()` helpers return the
    // length excluding it (i.e. strlen), so add 1 for each PEM buffer.
    // Passing the NUL-less length makes the parser fail with
    // MBEDTLS_ERR_X509_INVALID_FORMAT (-0x2180), which surfaces as a HTTPD
    // connection-accept error.
    conf.servercert =
        reinterpret_cast<const std::uint8_t*>(certs::kServerCertPem);
    conf.servercert_len = certs::server_cert_len() + 1;
    conf.prvtkey_pem =
        reinterpret_cast<const std::uint8_t*>(certs::kServerKeyPem);
    conf.prvtkey_len = certs::server_key_len() + 1;

    httpd_handle_t server = nullptr;
    esp_err_t err = httpd_ssl_start(&server, &conf);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "httpd_ssl_start failed: %s", esp_err_to_name(err));
        return err;
    }
    handle_ = server;

    // Install every route registered before start().
    for (auto& slot : routes_->slots) {
        if (!slot) {
            continue;
        }
        err = httpd_register_uri_handler(server, &slot->uri);
        if (err != ESP_OK) {
            ESP_LOGE(kTag, "failed to register %s: %s", slot->fragment.c_str(),
                     esp_err_to_name(err));
            httpd_ssl_stop(server);
            handle_ = nullptr;
            return err;
        }
    }

    ESP_LOGI(kTag, "HTTPS server listening on port 443");
    return ESP_OK;
}

esp_err_t HttpsServer::stop() {
    if (handle_ == nullptr) {
        return ESP_OK;
    }
    esp_err_t err = httpd_ssl_stop(static_cast<httpd_handle_t>(handle_));
    handle_ = nullptr;
    return err;
}

}  // namespace thermo
