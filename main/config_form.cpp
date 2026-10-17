// Form and Config conversion for the configuration UI (§5.1, §5.3).
#include "config_form.hpp"

#include <charconv>
#include <system_error>

namespace thermo::config_form {
namespace {

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

}  // namespace

std::string url_decode(const std::string& in, bool plus_is_space) {
    std::string out;
    out.reserve(in.size());
    for (std::size_t i = 0; i < in.size(); ++i) {
        const char c = in[i];
        if (c == '%' && i + 2 < in.size()) {
            auto hex = [](char h) -> int {
                if (h >= '0' && h <= '9')
                    return h - '0';
                if (h >= 'a' && h <= 'f')
                    return h - 'a' + 10;
                if (h >= 'A' && h <= 'F')
                    return h - 'A' + 10;
                return 0;
            };
            out.push_back(
                static_cast<char>((hex(in[i + 1]) << 4) | hex(in[i + 2])));
            i += 2;
        } else if (c == '+' && plus_is_space) {
            out.push_back(' ');
        } else {
            out.push_back(c);
        }
    }
    return out;
}

std::vector<Field> parse(const std::string& body) {
    std::vector<Field> fields;
    std::size_t pos = 0;
    while (pos < body.size()) {
        const std::size_t amp = body.find('&', pos);
        const std::string pair = body.substr(
            pos, amp == std::string::npos ? std::string::npos : amp - pos);
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

std::optional<std::string> get(const std::vector<Field>& fields,
                               const std::string& key) {
    for (const auto& [k, v] : fields) {
        if (k == key) {
            return v;
        }
    }
    return std::nullopt;
}

Config to_config(const Config& current, const std::vector<Field>& fields) {
    // Start from the current values so untouched fields survive.
    Config candidate = current;

    auto set_str = [&](const char* key, std::string& dst) {
        if (auto v = get(fields, key)) {
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
        if (auto v = get(fields, key)) {
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

    if (auto v = get(fields, "batt_cal_offset")) {
        candidate.batt_cal_offset =
            to_i32(*v, static_cast<std::int32_t>(candidate.batt_cal_offset));
    }
    if (auto v = get(fields, "batt_cal_gain")) {
        candidate.batt_cal_gain = to_f32(*v, candidate.batt_cal_gain);
    }
    candidate.batt_gate_en = get(fields, "batt_gate_en").has_value();

    // Per-sensor friendly names, submitted as map_<ROMID>.
    for (const auto& [key, value] : fields) {
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

    return candidate;
}

}  // namespace thermo::config_form
