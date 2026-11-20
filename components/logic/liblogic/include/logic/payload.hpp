// Batched-upload payload formatting (§9).
//
// Part of the `liblogic` component: the wire contract with the remote collector
// is board-independent, so the firmware and the host tests share one
// implementation. Deliberately free of ESP-IDF dependencies so it builds for
// the `linux` target. All formatting rules come from §9.1 (structure) and
// §9.3 (temperature serialization).
#pragma once

// NOLINTBEGIN
#pragma GCC diagnostic push  // NOLINT(ignored diagnostics)
#pragma GCC diagnostic pop
// NOLINTEND

#include <array>
#include <string>
#include <vector>

#include <cstdint>

#include "logic/limits.hpp"
#include "utility/static_string.hpp"
#include "utility/static_vector.hpp"

namespace thermo::logic::payload {

// One sample in the outgoing batch, already resolved to friendly names.
//
// Column order follows the mapping-table order (§5.1.3, §11 open question 11).
struct NamedSample {
    std::uint32_t age_s = 0;    // Seconds since boot at sample time.
    std::uint16_t batt_mv = 0;  // Battery voltage in millivolts.
    // Temperatures x100 in the same order as `names`; kTempNull == missing.
    std::array<std::int16_t, limits::kSensorCount> temps_c_x100{};
};

// A friendly sensor name (§5.1.3), and the names in column order, at most
// one per sensor slot. Both are fixed-capacity, so building a batch never
// allocates for the names.
using SensorName = utility::static_string<limits::kMaxSensorNameLength>;
using SensorNames = utility::static_vector<SensorName, limits::kSensorCount>;

// The configured device name (§5.1), and one formatted upload document.
// Both are fixed-capacity: `build_batch_json()` never allocates, and a
// document that does not fit its capacity aborts rather than losing the
// batch silently.
using DeviceName = utility::static_string<limits::kMaxDeviceNameLength>;
using Payload = utility::static_string<limits::kMaxPayloadBytes>;

// Widest one sample can be, with all three names at the validated maximum
// and fully JSON-escaped (a control character becomes six characters),
// plus the fixed keys and the longest temperature text.
inline constexpr std::size_t kMaxSampleJsonBytes =
    1 + 9 + 10 + 11 + 5 +
    limits::kSensorCount * (2 + 6 * limits::kMaxSensorNameLength + 2 + 7) + 1;

// Widest header: the escaped device name, the fixed keys, and a five-digit
// sample count.
inline constexpr std::size_t kMaxHeaderJsonBytes =
    10 + 2 + 6 * limits::kMaxDeviceNameLength + 43 + 5 + 12 + 2;

// Samples that always fit, whatever the configured names look like. A
// payload must hold at least one, or a full batch could never be sent.
inline constexpr std::size_t kMaxPayloadSamples =
    (limits::kMaxPayloadBytes - kMaxHeaderJsonBytes) / kMaxSampleJsonBytes;
static_assert(kMaxPayloadSamples >= 1,
              "the payload capacity must hold one worst-case sample");

// Builds the §9.1 JSON document for a batch of samples.
//
// `device` is the configured device name, `names` are the friendly names in
// column order (missing names are emitted as `null`), and `samples` are the
// buffered records oldest-last, matching the §9.1 example ordering.
//
// The returned string is a complete JSON object. No trailing newline.
//
// It must fit `kMaxPayloadBytes`. With ordinary names a sample is about
// 100 bytes, so a 1 KiB payload holds roughly nine samples; the worst-case
// count is `kMaxPayloadSamples`. A larger document aborts, which is a bad
// state: a batch that long means samples are still stuck in the RTC buffer
// and the abort loses them with it.
Payload build_batch_json(const DeviceName& device, const SensorNames& names,
                         const std::vector<NamedSample>& samples);

}  // namespace thermo::logic::payload
