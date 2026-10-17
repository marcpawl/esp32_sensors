// Batched-upload payload formatting (§9).
#include "payload.hpp"

#include <cstddef>

#include "temp_format.hpp"  // format_temp_c_x100, kTempNull

namespace thermo::payload {
namespace {

// Appends `s` as a JSON string literal, escaping the characters JSON requires
// and anything below U+0020. The device/network names come from the config
// page, so they are untrusted input and must not break the document.
void append_json_string(std::string& out, const std::string& s) {
    out.push_back('"');
    for (const char c : s) {
        switch (c) {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\b':
                out += "\\b";
                break;
            case '\f':
                out += "\\f";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    // Non-printable control character: emit \u00XX.
                    constexpr char kHex[] = "0123456789abcdef";
                    out += "\\u00";
                    out.push_back(
                        kHex[(static_cast<unsigned char>(c) >> 4) & 0xF]);
                    out.push_back(kHex[static_cast<unsigned char>(c) & 0xF]);
                } else {
                    out.push_back(c);
                }
                break;
        }
    }
    out.push_back('"');
}

void append_u32(std::string& out, std::uint32_t value) {
    out += std::to_string(value);
}

// Appends a temperature x100 as a §9.3 one-decimal JSON number, or `null`.
void append_temp(std::string& out, std::int16_t temp_c_x100) {
    if (temp_c_x100 == kTempNull) {
        out += "null";
        return;
    }
    out += format_temp_c_x100(temp_c_x100);
}

}  // namespace

std::string build_batch_json(const std::string& device,
                             const std::vector<std::string>& names,
                             const std::vector<NamedSample>& samples) {
    std::string out;
    // Rough sizing: header plus ~48 bytes per sample keeps reallocation down.
    out.reserve(128 + samples.size() * 64);

    out += "{\"device\":";
    append_json_string(out, device);
    out += ",\"unit\":\"C\",\"batt_unit\":\"mV\",\"batch_size\":";
    append_u32(out, static_cast<std::uint32_t>(samples.size()));
    out += ",\"samples\":[";

    for (std::size_t i = 0; i < samples.size(); ++i) {
        const NamedSample& sample = samples[i];
        if (i != 0) {
            out.push_back(',');
        }
        out += "{\"age_s\":";
        append_u32(out, sample.age_s);
        out += ",\"batt_mv\":";
        append_u32(out, sample.batt_mv);

        // §9.1: keys are the friendly names from the mapping. A sensor with no
        // configured name is emitted with a synthesized key rather than
        // dropped, so the array length matches §9.2's column layout.
        for (std::size_t s = 0; s < limits::kSensorCount; ++s) {
            out.push_back(',');
            if (s < names.size() && !names[s].empty()) {
                append_json_string(out, names[s]);
            } else {
                out += "\"sensor";
                append_u32(out, static_cast<std::uint32_t>(s + 1));
                out.push_back('"');
            }
            out.push_back(':');
            append_temp(out, sample.temps_c_x100[s]);
        }
        out.push_back('}');
    }

    out += "]}";
    return out;
}

}  // namespace thermo::payload
