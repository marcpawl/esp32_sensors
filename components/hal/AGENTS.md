# The HAL layer

This file documents the three components in `components/hal/` — the hardware
abstraction the firmware and the host tests share. Repository-wide guidance
(the build, the workflow, the quality gates) is in the root `AGENTS.md`; the
layering rules — which layer may require which — are in the `platform_layers`
skill (`.opencode/skills/platform_layers/SKILL.md`). The `hal_stub` component
has its own `AGENTS.md` for the mirrored ESP-IDF header rules.

## The three components

| Component | Kind | Linked by | Provides |
|---|---|---|---|
| `hal_common` | header-only | everything | the shared vocabulary: types and concepts |
| `hal_board` | `.cpp` | the firmware (`mcgowan/`) | the real, GPIO-backed drivers |
| `hal_stub` | `.cpp` | the host tests, the simulator application | the pin-free, software-driven stubs |

Hardware-specific drivers live in reusable ESP-IDF components rather than in
`mcgowan/src/`, so host tests can swap them for deterministic stubs. The root
`AGENTS.md` describes where the three sit and why every project needs its own
`EXTRA_COMPONENT_DIRS` entry for the directory.

**A project links exactly one implementation.** `hal_board` and `hal_stub`
expose the same headers and define the same symbols, so each project excludes
the other with `EXCLUDE_COMPONENTS` (set before `project()`): the firmware
excludes `hal_stub`, every `linux` project excludes `hal_board`. `hal_common`
is shared by everything and is never excluded.

## The header pair

Each HAL device has a header pair: the shared vocabulary in `hal_common` (e.g.
`hal/switch_types.hpp`) plus a per-implementation header of the same name
(e.g. `hal_board/switch.hpp` or `hal_stub/switch.hpp`) that includes
it and adds the concrete class. For the switch:

- `hal/switch_types.hpp` (`hal_common`) defines
  `thermo::hal::SwitchState` and `thermo::hal::SwitchCallback` — the
  single definition of the shared types. `SwitchCallback` is a move-only
  `std::move_only_function<void() noexcept>`: the handler reads the new state
  through the switch (`read()`), so it can capture the object it belongs to.
  The `noexcept` is part of the ISR contract — a callback that might throw
  cannot be stored, so a lambda written without `noexcept` refuses to convert.
  An empty callback unregisters.

  The handler runs in the GPIO interrupt, so `set_interrupt()` annotates its
  parameter `[[thermo::isr_safe]]` (see `utility/isr_safe.hpp`).
- `hal_board/switch.hpp` and `hal_stub/switch.hpp` (one each) define
  `thermo::hal::board::Switch` and `thermo::hal::stub::Switch` with an
  identical public interface (same ctor, inline `create()` factory, `init()`,
  `read()`, `gpio()`, `set_interrupt()`, `remove_interrupt()`). The shared
  types come from `hal/switch_types.hpp`, so the two implementations cannot
  drift from the vocabulary (or from each other).

The device is a **switch**, not a push button: the board's inputs are switch
poles, and a momentary button is just a switch a caller closes and opens again.

`gpio()` reports the pin the switch was built for, so a consumer can name it in
a diagnostic. `hal_board` installs its interrupt handlers per GPIO (one slot
each), so several switches can register handlers independently. The GPIO ISR
service is installed once by the aggregate `Board` constructor; `BoardStub`
calls the same entry point, which `hal_stub/gpio_stub.cpp` supplies as a no-op
because `esp_driver_gpio` registers an empty component on the `linux` target.

`Switch`'s destructor calls `remove_interrupt()`, so an ISR cannot call into a
destroyed switch. `remove_interrupt()` clears the callback and removes the
GPIO interrupt; on `hal_board` the removal is owned by an RAII
`InterruptHandle` (`hal_board/interrupt_handle.hpp`), whose destructor calls
`gpio_isr_handler_remove()`. A moved-from switch owns no handle, so destroying
it does not remove the moved-to switch's handler.

After removing the interrupt, `hal_board` asserts that the ISR is not still
inside the callback. A handler entered just before the removal would otherwise
read a switch that is being destroyed; it is a race that must never happen, so
the firmware aborts instead of proceeding.

## The GPIO strong type

Every device that is wired to a pin takes `hal::Gpio`, defined in
`hal/gpio.hpp` (`hal_common`), rather than an `int`:

- the factories (`Switch::create(Gpio)`, `Led::create(Gpio, name)`,
  `OneWire::create(Gpio)`);
- `Switch::gpio()` and `InterruptHandle`;
- every `BoardPins` field and every `thermo::pins` constant in
  `hal/pin_map.hpp`.

The constructor is `explicit` and the only way out is `as_int()`, so a pin
cannot be swapped with another number by accident, and every conversion is
greppable.

`Gpio` stores a plain `int`, not ESP-IDF's `gpio_num_t`. `hal_common` is
implementation-free and builds for the `linux` target, where
`esp_driver_gpio` is an empty component and its headers are not on the include
path, so `gpio_num_t` does not exist there. The cast to `gpio_num_t` lives in
`hal_board` — the only layer that calls the driver — next to the call it
feeds.

## The board names its device types

`BoardLike` requires every board to declare two nested aliases:
`T::LedType` (the type returned by `green()`) and `T::SwitchType` (the type
returned by `config_switch()`, shared by all three switches). A
board-independent template derives the concrete device type it needs from the
board, without naming `hal_board` or `hal_stub`.

## The HAL is semantics-free

`Switch::read()` returns `SwitchState::kOn` while the switch is closed and
`SwitchState::kOff` while it is open, and interpreting a transition (edge
detection, debouncing, hold timing) is the caller's job. Every device follows
the same rule: the HAL moves bytes and reports state, it never decides what
that state means.

## The 1-Wire bus returns fixed-capacity lists

The bus reports its results as two fixed-capacity lists:

- `OneWire::discover_sensors()` returns `hal::RomIdList`;
- `OneWire::read()` returns `hal::ReadingList`.

Both are `utility::static_vector`s with `hal::kMaxSensors` (§2.1,
three sensors) slots, so discovery and reading never touch the heap.
That is also why the firmware and the simulator build under
`-fanalyzer` without a diagnostic suppression around the bus.

More devices than the capacity is a wiring fault for the §2.1 bus, so the
list aborts rather than dropping a sensor. The `hal_stub` mirror stores its
collection the same way, and its `set_sensors()` takes the same list type.

## The mode switch is not one HAL device

ESP-IDF has no direct notion of a DPDT switch, so the board models each of its
two mode-sense poles as an independent `Switch` — `board.config_switch()` and
`board.run_switch()` — and the DPDT knowledge (both poles on is a hardware
fault, both off is the center/Off position, one of them selects Configure or
Run) lives in the logic layer. The stub's `Switch::set()` simulates the poles,
so bad hardware is reproducible in tests.

## The WiFi radio

The WiFi radio (§3.2, §3.9) follows the same header pair: `hal/wifi_types.hpp`
(`hal_common`) defines `AccessPointConfig`, `StationConfig`, `copy_text()`, the
length limits, and the `WifiRadioLike` concept; `hal_board/wifi.hpp` and
`hal_stub/wifi.hpp` define `WifiRadioBoard` / `WifiRadioStub` and alias
`using WifiRadio = ...`. The interface is `init()`, `start_ap()`/`stop_ap()`,
`start_station()`/`stop_station()` — the API and station are **independent**
(Configure mode may run both at once) and the radio is semantics-free (callers
own retry policy). An empty station SSID is rejected with
`ESP_ERR_INVALID_ARG`; callers must not start it. The stub adds observation
accessors (`initialized()`, `ap_running()`, `station_running()`, `ap_config()`,
`station_config()`), useful in tests.

## The aggregate board

**The aggregate board owns one of every device**, including the WiFi radio:
`BoardLike` (`hal/board_types.hpp`) requires `wifi()` (mutable and const), and
`init_all()` initializes the radio last. `init_all()` does **not** start the AP
or the station — the caller decides (Configure mode starts the AP, Run mode
starts the station). Reach the stub controls through the board, e.g.
`board.wifi().start_ap(cfg)` / `board.wifi().ap_running()`.

The two aggregate boards are **named differently on purpose**: `hal_board`'s is
`thermo::hal::board::Board` in `hal_board/board.hpp`, `hal_stub`'s is
`thermo::hal::stub::BoardStub` in `hal_stub/board_stub.hpp`. Nothing includes
one
header hoping the other implementation is on the include path, so a project
states which board it linked. The device headers are split the same way, with
`hal_board` under `hal_board/…` and `hal_stub` under `hal_stub/…`, and each
implementation provides its own `WifiRadio` alias. Code that must work with
either takes the `hal::BoardLike` template parameter and never names a concrete
board — see
`logic/led_task.hpp`, whose LED-cycle table and coverage checks are free
functions (`thermo::logic::ledtask::led_cycles()` / `has_cycle()`), so the
`static_assert`s naming every `LedState` need no board at all.

## Deep sleep

**Deep sleep is on the board, not a HAL device.** There is no third sleep
implementation to swap in, and sleeping powers down the *whole* board rather
than a peripheral, so `BoardLike` requires the two board-wide operations
directly: `deep_sleep_start(std::chrono::seconds)` (§3.8) and
`sleep_get_wakeup_causes()`. The vocabulary is in `hal/sleep_types.hpp`
(`hal_common`) — `WakeCause`, `WakeCauses`, `wake_cause_bit()`, `woke_from()` —
and is deliberately dependency-free (only `<cstdint>`): `esp_sleep.h` is not
self-contained on the `linux` target (it includes `hal/gpio_types.h`, which is
not on the include path of a project that links no GPIO driver), and the
ESP-IDF linux port registers no `sleep_modes.c`, so
`esp_sleep_get_wakeup_causes()` is unlinkable there.

`WakeCause` names ESP-IDF's `esp_sleep_source_t` **bit positions**
(`kUndefined` = 0, `kTimer` = 4) so the hardware board passes the ESP-IDF
bitmap straight through; `board.cpp` `static_assert`s that the values still
line up. The wake cause is a bitmap because several sources can fire at once.

`hal_board` calls `esp_sleep_enable_timer_wakeup()` + `esp_deep_sleep_start()`
(the §3.8 pair) and `deep_sleep_start()` is `[[noreturn]]` there. `hal_stub`
records the duration (`last_deep_sleep()`), arms the wake the real board would
produce — `deep_sleep_start()` sets the cause to `WakeCause::kTimer` —
and **returns**. A fresh stub reports a cold boot
(`wake_cause_bit(WakeCause::kUndefined)`), and `set_wakeup_causes()` overrides
either, so a test can simulate any wake. The `BoardLike` concept can therefore
only require that the call is *well-formed* — not `[[noreturn]]` — and no
caller may rely on sleep returning. That asymmetry is deliberate: it is what
makes the sleep/backoff decision host-testable. Run mode now sleeps through the
board (`board.deep_sleep_start(std::chrono::seconds{sleep_s})`), and the RTC
boot policy does the same in reverse: `rtc_state_init(board)` asks
`sleep_get_wakeup_causes()` whether the retained block survives this boot.

## How the HAL is tested

The vocabulary's own tests live in `test-hal-common/`, and the stub devices'
tests live in `test-hal-stub/` (the hardware-backed devices are covered by
on-target tests). The vocabulary the former pins is the shared one — the
`BoardLike` concept and the deep-sleep types.

- **`test-hal-common/`** covers the implementation-free `hal_common`
  vocabulary. `test_hal_gpio.cpp` pins the `Gpio` value type (default pin 0,
  `as_int()` round-trip, value equality, no implicit conversion either way,
  `constexpr`) and the typed §2.4 pin map. `test_hal_board_types.cpp` pins the
  `BoardLike` contract from the other side: `BoardPins` defaults to the §2.4
  pin map, a model spells out every
  requirement using the real stub devices so the concept accepts it, a model
  without the nested `LedType` / `SwitchType` aliases is rejected, and a
  non-class, a class with none of the board interface, and a reference are each
  rejected. `test_hal_sleep_types.cpp` pins the wake-cause vocabulary
  (`WakeCause` at ESP-IDF's bit positions, `wake_cause_bit()`, and
  `woke_from()`).
- **`test-hal-stub/`** covers the pin-free `hal_stub` devices. Each device's
  own file exercises its stub contract; where the vocabulary defines a device
  concept, the file checks that too:
  `test_hal_led.cpp` (`LedLike`), `test_hal_switch.cpp` (the switch device
  contract — the `gpio()` getter, capturing handlers, two independent
  switches, move, and the `set()` control the mode-sense poles use),
  `test_hal_onewire.cpp` (`OneWireLike` plus `set_sensors()`),
  `test_hal_chip_temperature.cpp` (`ChipTemperatureLike`), and
  `test_hal_wifi.cpp` (`WifiRadioLike`, AP/station start/stop independence, and
  the empty-SSID rejection). `test_freertos_timers.cpp` verifies that FreeRTOS
  software timers create, fire exactly once, re-arm, and stay silent after a
  cancel. `test_hal_board.cpp` covers the aggregate `BoardStub`: that
  `config_switch()` / `run_switch()` / `wifi()` are wired up, that `init_all()`
  initializes but does not start the radio, and that `deep_sleep_start()`
  records the duration while `sleep_get_wakeup_causes()` / `set_wakeup_causes()`
  model the next boot's wake cause.

Run them with
`./scripts/dev.py test-linux components/hal/tests/test-hal-common` and
`./scripts/dev.py test-linux components/hal/tests/test-hal-stub`, or through
`./scripts/dev.py test-linux`, which runs every host suite.

The on-target `esp_timer` suite is the exception that needs hardware: the same
`esp_timer_tests` cases run against the chip in
`components/hal/tests/esp_timer/board/`, and its `test.py` driver flashes the
board and waits for the sentinel that `main.cpp` prints after Unity finishes.
Run it with `./scripts/dev.py test-esp32`. The host equivalent in
`components/hal/tests/esp_timer/stub/` links `hal_stub`; its
`esp_timer_stub.cpp` deliberately matches the chip's behaviour apart from
timer resolution.
