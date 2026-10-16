// Shared, implementation-free types for the ESP32 on-die chip temperature
// sensor (§2.1, §10).
//
// This header is the *single* definition of the chip-temperature vocabulary —
// the `ChipTemperatureLike` concept — used by both implementations:
//
//   - `hal_board` — real implementation backed by the ESP-IDF internal
//     temperature sensor driver (`esp_driver_tsens`).
//   - `hal_stub`  — deterministic stub used under QEMU and host tests.
//
// Each implementation provides its own `hal/chip_temperature.hpp` that includes
// this header and adds the concrete `ChipTemperature` class, so there is
// exactly one definition of the shared contract and the two implementations
// cannot drift. (A project still links exactly one of `hal_board` / `hal_stub`,
// but both `REQUIRES hal_common`.)
#pragma once

#include <concepts>
#include <cstdint>
#include <type_traits>
#include <utility>

#include "esp_err.h"

namespace thermo::hal {

// Concept: a chip-temperature type that exposes the shared interface — an
// inline static `create()` factory (taking no parameters), an `init()`, and a
// `read()` that yields the temperature in hundredths of a degree Celsius.
//
// The reading is `std::int16_t` in x100 °C (e.g. 2350 == 23.50 °C), matching
// the DS18B20 readings so the two can be stored interchangeably.
template<typename T>
concept ChipTemperatureLike = std::is_class_v<T> && requires(T& sensor) {
    { T::create() } -> std::same_as<T>;
    { std::declval<T&>().init() } -> std::same_as<esp_err_t>;
    { std::declval<T&>().read() } -> std::same_as<std::int16_t>;
};

}  // namespace thermo::hal
