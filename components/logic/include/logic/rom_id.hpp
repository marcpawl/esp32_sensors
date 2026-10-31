// DS18B20 ROM-ID formatting and parsing (§5.1.3, §6.1).
//
// Part of the `logic` component: the textual form of a ROM ID is
// board-independent vocabulary shared by the sensor layer, the configuration
// UI, and the payload. The ROM ID *type* itself (`thermo::hal::RomId`) is the
// HAL's data, defined in the implementation-free `hal_common` component
// (`hal/ds18b20_types.hpp`); converting it to and from text is application
// behaviour and lives here, as free functions, so no behaviour is attached to
// the hardware vocabulary.
//
// Free of ESP-IDF dependencies, so the hex round-trip and the validation rules
// build for the `linux` target and are covered by `test/main/test_rom_id.cpp`.
#pragma once

// NOLINTBEGIN
#pragma GCC diagnostic push  // NOLINT(ignored diagnostics)
#pragma GCC diagnostic pop
// NOLINTEND

#include <optional>
#include <string>
#include <string_view>

#include "hal/ds18b20_types.hpp"  // RomId

namespace thermo {

// Renders `rom` as 16 uppercase hex characters, no separator (§5.1.3, §6.1).
std::string rom_id_to_hex(const hal::RomId& rom);

// Parses exactly 16 hex characters (case-insensitive) into a ROM ID. Returns
// `std::nullopt` for a wrong length or a non-hex character; it never silently
// produces a different identity (the config keys are stored in the canonical
// `rom_id_to_hex()` form, §5.1.3).
std::optional<hal::RomId> rom_id_from_hex(std::string_view hex);

}  // namespace thermo
