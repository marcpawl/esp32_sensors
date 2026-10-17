// Batched-upload payload formatting (§9).
//
// This module is deliberately free of ESP-IDF dependencies so that it can be
// built and unit-tested on the host target. All formatting rules come from
// §9.1 (structure) and §9.3 (temperature serialization).
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "constants.hpp"

namespace thermo::payload {

// One sample in the outgoing batch, already resolved to friendly names.
//
// Column order follows the mapping-table order (§5.1.3, §11 open question 11).
struct NamedSample {
    std::uint32_t age_s = 0;    // Seconds since boot at sample time.
    std::uint16_t batt_mv = 0;  // Battery voltage in millivolts.
    // Temperatures x100 in the same order as `names`; kTempNull == missing.
    std::array<std::int16_t, limits::kSensorCount> temps_c_x100{};
};

// Builds the §9.1 JSON document for a batch of samples.
//
// `device` is the configured device name, `names` are the friendly names in
// column order (missing names are emitted as `null`), and `samples` are the
// buffered records oldest-last, matching the §9.1 example ordering.
//
// The returned string is a complete JSON object. No trailing newline.
std::string build_batch_json(const std::string& device,
                             const std::vector<std::string>& names,
                             const std::vector<NamedSample>& samples);

}  // namespace thermo::payload
