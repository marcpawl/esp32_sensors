// HTTPS configuration server (§6).
#include "https_server.hpp"

#include <charconv>
#include <cstring>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

#include "app_config_store.hpp"
#include "config_page.hpp"
#include "constants.hpp"
#include "esp_log.h"
#include "esp_https_server.h"
#include "self_signed_cert.hpp"

namespace thermo {
namespace {

constexpr const char* kTag = "https";

// Context passed to the C URI handlers.
struct HandlerContext {
    Config* cfg;
    ScanProvider* scan_provider;
    SaveHandler* save_handler;
    // Warning produced by the most recent save (e.g. NTP failure), shown on
    // the following GET /saved. Empty when there is nothing to report.
    std::string last_warning;
};

// Sends `body` with the given status and content type.
esp_err_t send_body(httpd_req_t* req, const std::string& body,
                    const char* content_type, const char* status = "200 OK") {
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, content_type);
    return httpd_resp_send(req, body.c_str(), body.size());
}

// Collects the request body (with a sane cap) into a string.
std::optional<std::string> read_body(httpd_req_t* req) {
    constexpr std::size_t kMaxBody = 8192;
    if (static_cast<std::size_t>(req->content_len) > kMaxBody) {
        return std::nullopt;
    }
    std::string body(req->content_len, '\0');
    std::size_t received = 0;
    while (received < body.size()) {
        const int ret = httpd_req_recv(req, body.data() + received,
                                       body.size() - received);
        if (ret <= 0) {
            if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
                continue; // Retry on timeout.
            }
            return std::nullopt;
        }
        received += static_cast<std::size_t>(ret);
    }
    return body;
}

// URL-decodes a form component into UTF-8 (byte-for-byte; no re-encoding).
std::string url_decode(const std::string& in, bool plus_is_space) {
    std::string out;
    out.reserve(in.size());
    for (std::size_t i = 0; i < in.size(); ++i) {
        const char c = in[i];
        if (c == '%' && i + 2 < in.size()) {
            auto hex = [](char h) -> int {
                if (h >= '0' && h <= '9') return h - '0';
                if (h >= 'a' && h <= 'f') return h - 'a' + 10;
                if (h >= 'A' && h <= 'F') return h - 'A' + 10;
                return 0;
            };
            out.push_back(static_cast<char>((hex(in[i + 1]) << 4) |
                                            hex(in[i + 2])));
            i += 2;
        } else if (c == '+' && plus_is_space) {
            out.push_back(' ');
        } else {
            out.push_back(c);
        }
    }
    return out;
}

// Parses application/x-www-form-urlencoded data into a simple key/value map.
std::vector<std::pair<std::string, std::string>> parse_form(
    const std::string& body) {
    std::vector<std::pair<std::string, std::string>> fields;
    std::size_t pos = 0;
    while (pos < body.size()) {
        const std::size_t amp = body.find('&', pos);
        const std::string pair =
            body.substr(pos, amp == std::string::npos ? std::string::npos
                                                      : amp - pos);
        const std::size_t eq = pair.find('=');
        if (eq != std::string::npos) {
            fields.emplace_back(url_decode(pair.substr(0, eq), true),
                                url_decode(pair.substr(eq + 1), true));
        }
        if (amp == std::string::npos) {
            break;
        }
        pos = amp + 1;
    }
    return fields;
}

std::optional<std::string> get_field(
    const std::vector<std::pair<std::string, std::string>>& fields,
    const std::string& key) {
    for (const auto& [k, v] : fields) {
        if (k == key) {
            return v;
        }
    }
    return std::nullopt;
}

std::uint32_t to_u32(const std::string& s, std::uint32_t def) {
    std::uint32_t value = 0;
    const auto* first = s.data();
    const auto* last = s.data() + s.size();
    const auto res = std::from_chars(first, last, value);
    if (res.ec != std::errc{} || res.ptr != last) {
        return def;
    }
    return value;
}

std::int32_t to_i32(const std::string& s, std::int32_t def) {
    std::int32_t value = 0;
    const auto* first = s.data();
    const auto* last = s.data() + s.size();
    const auto res = std::from_chars(first, last, value);
    if (res.ec != std::errc{} || res.ptr != last) {
        return def;
    }
    return value;
}

float to_f32(const std::string& s, float def) {
    float value = 0.0F;
    const auto* first = s.data();
    const auto* last = s.data() + s.size();
    const auto res = std::from_chars(first, last, value);
    if (res.ec != std::errc{} || res.ptr != last) {
        return def;
    }
    return value;
}

// --- URI handlers ----------------------------------------------------------

// GET / — the configuration form (§6).
esp_err_t handler_root(httpd_req_t* req) {
    auto* ctx = static_cast<HandlerContext*>(req->user_ctx);
    const ScanSnapshot scan = (*ctx->scan_provider)();
    const std::string html =
        config_page::render_form(*ctx->cfg, scan, /*errors=*/{});
    return send_body(req, html, "text/html");
}

// GET /scan — JSON snapshot (§6.1).
esp_err_t handler_scan(httpd_req_t* req) {
    auto* ctx = static_cast<HandlerContext*>(req->user_ctx);
    const ScanSnapshot scan = (*ctx->scan_provider)();
    const std::string json = config_page::render_scan_json(*ctx->cfg, scan);
    return send_body(req, json, "application/json");
}

// POST /save — validate, persist, NTP sync, then redirect (§5.3).
esp_err_t handler_save(httpd_req_t* req) {
    auto* ctx = static_cast<HandlerContext*>(req->user_ctx);
    const auto body = read_body(req);
    if (!body) {
        return send_body(req, "Bad Request", "text/plain", "400 Bad Request");
    }

    const auto fields = parse_form(*body);

    // Build a candidate config from the submitted form, starting from the
    // current values so untouched fields survive.
    Config candidate = *ctx->cfg;
    auto set_str = [&](const char* key, std::string& dst) {
        if (auto v = get_field(fields, key)) {
            dst = *v;
        }
    };
    set_str("ap_ssid", candidate.ap_ssid);
    set_str("ap_pass", candidate.ap_pass);
    set_str("sta_ssid", candidate.sta_ssid);
    set_str("sta_pass", candidate.sta_pass);
    set_str("dest_url", candidate.dest_url);
    set_str("dev_name", candidate.dev_name);
    set_str("ntp_server", candidate.ntp_server);

    auto set_u32 = [&](const char* key, std::uint32_t& dst) {
        if (auto v = get_field(fields, key)) {
            dst = to_u32(*v, dst);
        }
    };
    set_u32("interval_s", candidate.interval_s);
    set_u32("retry_base_s", candidate.retry_base_s);
    set_u32("retry_max_s", candidate.retry_max_s);
    set_u32("batch_size", candidate.batch_size);
    set_u32("batt_low_mv", candidate.batt_low_mv);
    set_u32("batt_high_mv", candidate.batt_high_mv);
    set_u32("batt_r_top", candidate.batt_r_top);
    set_u32("batt_r_bot", candidate.batt_r_bot);

    if (auto v = get_field(fields, "batt_cal_offset")) {
        candidate.batt_cal_offset =
            to_i32(*v, static_cast<std::int32_t>(candidate.batt_cal_offset));
    }
    if (auto v = get_field(fields, "batt_cal_gain")) {
        candidate.batt_cal_gain = to_f32(*v, candidate.batt_cal_gain);
    }
    candidate.batt_gate_en = get_field(fields, "batt_gate_en").has_value();

    // Per-sensor friendly names, submitted as map_<ROMID>.
    for (auto& [key, value] : fields) {
        if (key.rfind("map_", 0) == 0) {
            const std::string rom = key.substr(4);
            bool replaced = false;
            for (auto& m : candidate.mappings) {
                if (m.rom == rom) {
                    m.name = value;
                    replaced = true;
                    break;
                }
            }
            if (!replaced) {
                candidate.mappings.push_back({rom, value});
            }
        }
    }

    // Validate (§5.3 step 1). On failure, re-render with errors and do NOT
    // touch saved_time or NVS (§5.3 step 2).
    const ValidationResult result = validation::validate(candidate);
    if (!result.ok) {
        const ScanSnapshot scan = (*ctx->scan_provider)();
        const std::string html =
            config_page::render_form(candidate, scan, result.errors);
        return send_body(req, html, "text/html", "400 Bad Request");
    }

    // Persist the validated configuration (§5.3 step 3).
    esp_err_t err = config_save(candidate);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "config_save failed: %s", esp_err_to_name(err));
        return send_body(req, "Failed to persist configuration", "text/plain",
                         "500 Internal Server Error");
    }
    *ctx->cfg = candidate;

    // NTP sync for saved_time; the handler reports any warning (§5.3 step 4-6).
    ctx->last_warning.clear();
    if (ctx->save_handler) {
        if (auto warning = (*ctx->save_handler)(*ctx->cfg)) {
            ctx->last_warning = *warning;
        }
    }

    // Redirect to /saved (§6).
    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "/saved");
    return httpd_resp_send(req, nullptr, 0);
}

// GET /saved — confirmation page (§6).
esp_err_t handler_saved(httpd_req_t* req) {
    auto* ctx = static_cast<HandlerContext*>(req->user_ctx);
    const std::string html =
        config_page::render_saved(*ctx->cfg, ctx->last_warning);
    return send_body(req, html, "text/html");
}

HandlerContext g_ctx{};

} // namespace

HttpsServer::HttpsServer(Config& cfg, ScanProvider scan_provider,
                         SaveHandler save_handler)
    : cfg_(cfg),
      scan_provider_(std::move(scan_provider)),
      save_handler_(std::move(save_handler)) {}

esp_err_t HttpsServer::start() {
    // The static context outlives the server; callbacks only dereference
    // pointers into the member callables below.
    g_ctx.cfg = &cfg_;
    g_ctx.scan_provider = &scan_provider_;
    g_ctx.save_handler = &save_handler_;

    httpd_ssl_config_t conf = HTTPD_SSL_CONFIG_DEFAULT();
    conf.port_secure = 443;
    conf.servercert = reinterpret_cast<const std::uint8_t*>(
        certs::kServerCertPem);
    conf.servercert_len = certs::server_cert_len();
    conf.prvtkey_pem = reinterpret_cast<const std::uint8_t*>(
        certs::kServerKeyPem);
    conf.prvtkey_len = certs::server_key_len();

    httpd_handle_t server = nullptr;
    esp_err_t err = httpd_ssl_start(&server, &conf);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "httpd_ssl_start failed: %s", esp_err_to_name(err));
        return err;
    }
    handle_ = server;

    // Routes per §6. Built as zero-initialized structs then assigned, to avoid
    // C++ designated-initializer restrictions on the trailing fields.
    auto make_route = [](const char* uri, httpd_method_t method,
                         esp_err_t (*handler)(httpd_req_t*)) {
        httpd_uri_t route = {};
        route.uri = uri;
        route.method = method;
        route.handler = handler;
        // The static context outlives the server; handlers dereference the
        // pointers it holds into the member callables.
        route.user_ctx = &g_ctx;
        return route;
    };
    const httpd_uri_t routes[] = {
        make_route("/", HTTP_GET, handler_root),
        make_route("/scan", HTTP_GET, handler_scan),
        make_route("/save", HTTP_POST, handler_save),
        make_route("/saved", HTTP_GET, handler_saved),
    };
    for (const auto& route : routes) {
        ESP_ERROR_CHECK(httpd_register_uri_handler(server, &route));
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

} // namespace thermo
