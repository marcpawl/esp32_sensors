// Shared, implementation-free types for the DS18B20 1-Wire bus (§2.1, §2.4).
//
// This header is the *single* definition of the 1-Wire bus vocabulary — the
// `OneWireLike` concept — used by both bus implementations:
//
//   - `hal_board` — real, GPIO-backed bit-banged 1-Wire bus.
//   - `hal_stub`  — deterministic stub used under QEMU and host tests.
//
// Each implementation provides its own `hal/onewire.hpp` that includes this
// header and adds the concrete `OneWire` class, so there is exactly one
// definition of the shared contract and the two implementations cannot drift.
// (A project still links exactly one of `hal_board` / `hal_stub`, but both
// `REQUIRES hal_common`.)
//
// The bus exposes the ROM IDs present on the wire and a reading (temperature)
// for each of them. Mapping readings to friendly names is the caller's job.
#pragma once

#include <concepts>
#include <type_traits>
#include <vector>

#include "esp_err.h"
#include "hal/ds18b20_types.hpp"  // RomId, DS18B20Reading

namespace thermo::hal {

// Concept: a 1-Wire bus type that exposes the shared interface — an inline
// static `create(gpio)` factory, an `init()`, a `discover_sensors()` that
// returns the ROM IDs found on the bus, and a `read()` that returns a reading
// (ROM ID + temperature) for each sensor. Both implementations satisfy it, so
// a project can `static_assert` which implementation it linked in.
template<typename T>
concept OneWireLike = std::is_class_v<T> && requires(int gpio) {
    { T::create(gpio) } -> std::same_as<T>;
    { std::declval<T&>().init() } -> std::same_as<esp_err_t>;
    {
        std::declval<T&>().discover_sensors()
    } -> std::same_as<std::vector<RomId>>;
    { std::declval<T&>().read() } -> std::same_as<std::vector<DS18B20Reading>>;
};

}  // namespace thermo::hal
