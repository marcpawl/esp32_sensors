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
// Each implementation provides its own aggregate-board header
// (`hal_board/board.hpp` or `hal_stub/board_stub.hpp`) that includes this
// header and adds the concrete class, so there is exactly one definition of
// the shared contract and the two implementations cannot drift. (A project
// still links exactly one of `hal_board` / `hal_stub`, but both
// `REQUIRES hal_common`.)
//
// The board is a convenience aggregate: it owns one instance of each HAL
// device and exposes them. It adds no behaviour of its own beyond delegating
// `init_all()` to the devices' `init()` and owning the two board-wide
// operations that are not a peripheral — entering deep sleep (§3.8) and
// reporting why the last wake happened; the semantics of each device live in
// that device's own HAL.
//
// The one-shot timer is owned by the board too, but exposed through a *member
// typedef* rather than a device accessor: `BoardLike` requires a public
// `TimerHandle` type and a `register_timer()` method, so a caller arms a timer
// through the board and holds the returned handle without naming either
// implementation's classes. `TimerHandle` must satisfy `TimerLike` (see
// `hal/timer_types.hpp`), which is all a holder needs to cancel one.
#pragma once

// NOLINTBEGIN
#pragma GCC diagnostic push  // NOLINT(ignored diagnostics)
#include "esp_err.h"
#pragma GCC diagnostic pop
// NOLINTEND

#include <chrono>
#include <concepts>
#include <functional>
#include <type_traits>

#include "hal/chip_temperature_types.hpp"
#include "hal/led_types.hpp"
#include "hal/onewire_types.hpp"
#include "hal/pin_map.hpp"
#include "hal/push_button_types.hpp"
#include "hal/sleep_types.hpp"
#include "hal/timer_types.hpp"
#include "hal/wifi_types.hpp"

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
//
// The one-shot timer is required as a *member typedef* plus a method rather
// than as a device accessor: `TimerHandle` is the type the caller holds, and
// `register_timer()` arms one. Because `TimerHandle` is spelled by the
// implementation, `BoardLike` stays implementation-free — it names only the
// `TimerLike` concept, so a project can link either board and board-
// independent code can cancel a timer without knowing which mechanism armed it.
template<typename T>
concept BoardLike = std::is_class_v<T> && requires(
                                              T& board, T const& const_board,
                                              std::chrono::seconds duration,
                                              std::chrono::milliseconds delay,
                                              std::function<void()> callback) {
    // The board names the handle type it hands back, and that type can be
    // cancelled. A board without a public `TimerHandle` — or one whose
    // handle has no `cancel()` — cannot model this.
    requires TimerLike<typename T::TimerHandle>;
    {
        board.register_timer(delay, callback)
    } -> std::same_as<typename T::TimerHandle>;
    { T::create(BoardPins{}) } -> std::same_as<T>;
    { board.init_all() } -> std::same_as<esp_err_t>;
    { board.deep_sleep_start(duration) } -> std::same_as<void>;
    { const_board.sleep_get_wakeup_causes() } -> std::same_as<WakeCauses>;

    // Every accessor returns a type satisfying the matching `hal_common`
    // device concept. Naming only the concepts keeps this header free of
    // either implementation: `hal_board` and `hal_stub` both depend on
    // `hal_common`, never the other way round.
    requires ChipTemperatureLike<
        std::remove_cvref_t<decltype(board.chip_temperature())>>;
    requires ChipTemperatureLike<
        std::remove_cvref_t<decltype(const_board.chip_temperature())>>;
    requires OneWireLike<std::remove_cvref_t<decltype(board.one_wire())>>;
    requires OneWireLike<std::remove_cvref_t<decltype(const_board.one_wire())>>;
    requires LedLike<std::remove_cvref_t<decltype(board.green())>>;
    requires LedLike<std::remove_cvref_t<decltype(const_board.green())>>;
    requires LedLike<std::remove_cvref_t<decltype(board.red())>>;
    requires LedLike<std::remove_cvref_t<decltype(const_board.red())>>;
    requires PushButtonLike<std::remove_cvref_t<decltype(board.button())>>;
    requires PushButtonLike<
        std::remove_cvref_t<decltype(const_board.button())>>;
    requires PushButtonLike<
        std::remove_cvref_t<decltype(board.config_button())>>;
    requires PushButtonLike<
        std::remove_cvref_t<decltype(const_board.config_button())>>;
    requires PushButtonLike<std::remove_cvref_t<decltype(board.run_button())>>;
    requires PushButtonLike<
        std::remove_cvref_t<decltype(const_board.run_button())>>;
    requires WifiRadioLike<std::remove_cvref_t<decltype(board.wifi())>>;
    requires WifiRadioLike<std::remove_cvref_t<decltype(const_board.wifi())>>;
};

}  // namespace thermo::hal
