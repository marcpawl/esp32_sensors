// Batched-upload payload formatting (§9).
//
// Part of the `liblogic` component so the firmware and the host tests share one
// implementation; free of ESP-IDF dependencies, so it builds for the `linux`
// target (the same rationale as `logic/src/temp_format.cpp`). The temperature
// serialization itself is `logic/temp_format.hpp`; this file owns only the
// §9.1 document structure and the JSON escaping.
#include "logic/payload.hpp"

// NOLINTBEGIN
#pragma GCC diagnostic push  // NOLINT(ignored diagnostics)
#pragma GCC diagnostic pop
// NOLINTEND

#include <cstddef>
#include <string_view>

#include "logic/temp_format.hpp"  // format_temp_c_x100, kTempNull

namespace thermo::logic::payload {
namespace {

// Appends `s` as a JSON string literal, escaping the characters JSON requires
// and anything below U+0020. The device/network names come from the config
// page, so they are untrusted input and must not break the document.
void append_json_string(Payload& out, std::string_view s)
{
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

// Appends `value` in decimal without a temporary string.
void append_u32(Payload& out, std::uint32_t value)
{
    char digits[10]{};
    std::size_t count = 0;
    do {
        digits[count++] = static_cast<char>('0' + (value % 10));
        value /= 10;
    } while (value != 0);
    while (count > 0) {
        out.push_back(digits[--count]);
    }
}

// Appends a temperature x100 as a §9.3 one-decimal JSON number, or `null`.
void append_temp(Payload& out, std::int16_t temp_c_x100)
{
    if (temp_c_x100 == kTempNull) {
        out += "null";
        return;
    }
    out += format_temp_c_x100(temp_c_x100).c_str();
}

}  // namespace

Payload build_batch_json(const DeviceName& device, const SensorNames& names,
                         const std::vector<NamedSample>& samples)
{
    // Fixed capacity: an over-long batch aborts here. That is a bad state,
    // because it means many samples are still buffered and untransmitted,
    // and the abort loses them with the RTC buffer.
    Payload out;

    out += "{\"device\":";
    append_json_string(out, device.c_str());
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
                append_json_string(out, names[s].c_str());
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

}  // namespace thermo::logic::payload
