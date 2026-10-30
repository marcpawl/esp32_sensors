// Shared, implementation-free types for the aggregate Board — the collection of
// HAL devices on the development board.
//
// This header is the *single* definition of the board's vocabulary — the
// `BoardPins` description and the `BoardLike` concept — used by both board
// implementations:
//
//   - `hal_board` — real, GPIO-backed devices for the development board.
//   - `hal_stub`  — deterministic, pin-free devices used by the host tests.
//
// Each implementation provides its own `hal/board.hpp` that includes this
// header and adds the concrete `Board` class, so there is exactly one
// definition of the shared contract and the two implementations cannot drift.
// (A project still links exactly one of `hal_board` / `hal_stub`, but both
// `REQUIRES hal_common`.)
//
// The board is a convenience aggregate: it owns one instance of each HAL
// device and exposes them. It adds no behaviour of its own beyond delegating
// `init_all()` to the devices' `init()` and owning the two board-wide
// operations that are not a peripheral — entering deep sleep (§3.8) and
// reporting why the last wake happened; the semantics of each device live in
// that device's own HAL.
#pragma once

// NOLINTBEGIN
#pragma GCC diagnostic push  // NOLINT(ignored diagnostics)
#include "esp_err.h"
#pragma GCC diagnostic pop
// NOLINTEND

#include <chrono>
#include <concepts>
#include <type_traits>

#include "hal/chip_temperature.hpp"
#include "hal/led.hpp"
#include "hal/onewire.hpp"
#include "hal/pin_map.hpp"
#include "hal/push_button.hpp"
#include "hal/sleep_types.hpp"
#include "hal/wifi.hpp"

namespace thermo::hal {

// GPIO assignment (and labels) needed to build a Board. Passing one aggregate
// keeps `create()` readable and lets a caller document the wiring in one place.
//
// The GPIO defaults are the real development-board pin map (§2.4, defined in
// `hal/pin_map.hpp`), so a caller on the reference hardware can build a board
// with `Board::create(BoardPins{})` and only override what differs (e.g. in
// tests, where the pins are ignored).
//
// `green_name` / `red_name` are human-readable LED labels (a pointer to a
// string literal is expected); the hardware implementation ignores them and the
// stub uses them for logging. The chip-temperature sensor has no pin and is not
// represented here.
struct BoardPins {
    // 1-Wire data pin for the DS18B20 bus.
    int one_wire = pins::kWireBus;
    // LED output pins and labels.
    int green_led = pins::kGreenLed;
    int red_led = pins::kRedLed;
    const char* green_name = "Green";
    const char* red_name = "Red";
    // Push-button input pin. The reference board has no dedicated button, so
    // this defaults to 0 and callers should set it explicitly if wired.
    int button = 0;
    // Mode-selection push buttons, one per mode-sense pole. ESP-IDF has no
    // direct notion of the 3-position (DPDT) switch, so the board reports each
    // sense pole as an independent button; mapping the pair to a mode is logic
    // (§3.1).
    int config_button = pins::kConfig;
    int run_button = pins::kRun;
};

// Concept: a board type that owns one of each HAL device and exposes them
// through accessors, plus an `init_all()`. Both implementations satisfy it, so
// a project can `static_assert` which implementation it linked in.
//
// The accessors are declared for both a mutable and a const `Board`, so callers
// can reach a device's `read()` (and, for stubs, its `set_*()`) through either.
//
// Deep sleep (§3.8) is part of the contract, not a device accessor: it is the
// board that powers down, so the board owns both halves of it — sleeping for a
// duration (`deep_sleep_start()`) and reporting why the last wake happened
// (`sleep_get_wakeup_causes()`). See `hal/sleep_types.hpp` for the wake-cause
// vocabulary.
//
// The concept can only require that `deep_sleep_start()` is *callable*. On the
// hardware board it never returns (`esp_deep_sleep_start()` is `[[noreturn]]`);
// the stub must return, so a host test can carry on and assert the recorded
// request. `[[noreturn]]` is deliberately not part of the contract, and no
// caller may rely on the sleep returning.
template<typename T>
concept BoardLike =
    std::is_class_v<T> &&
    requires(T& board, T const& const_board, std::chrono::seconds duration) {
        { T::create(BoardPins{}) } -> std::same_as<T>;
        { board.init_all() } -> std::same_as<esp_err_t>;
        { board.deep_sleep_start(duration) } -> std::same_as<void>;
        { const_board.sleep_get_wakeup_causes() } -> std::same_as<WakeCauses>;
        { board.chip_temperature() } -> std::same_as<ChipTemperature&>;
        {
            const_board.chip_temperature()
        } -> std::same_as<ChipTemperature const&>;
        { board.one_wire() } -> std::same_as<OneWire&>;
        { const_board.one_wire() } -> std::same_as<OneWire const&>;
        { board.green() } -> std::same_as<Led&>;
        { const_board.green() } -> std::same_as<Led const&>;
        { board.red() } -> std::same_as<Led&>;
        { const_board.red() } -> std::same_as<Led const&>;
        { board.button() } -> std::same_as<PushButton&>;
        { const_board.button() } -> std::same_as<PushButton const&>;
        { board.config_button() } -> std::same_as<PushButton&>;
        { const_board.config_button() } -> std::same_as<PushButton const&>;
        { board.run_button() } -> std::same_as<PushButton&>;
        { const_board.run_button() } -> std::same_as<PushButton const&>;
        { board.wifi() } -> std::same_as<WifiRadio&>;
        { const_board.wifi() } -> std::same_as<WifiRadio const&>;
    };

}  // namespace thermo::hal
