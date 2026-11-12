# The HAL layer

This file documents the three components in `components/hal/` — the hardware
abstraction the firmware and the host tests share. Repository-wide guidance
(the build, the workflow, the quality gates) is in the root `AGENTS.md`; the
layering rules — which layer may require which — are in the `platform_layers`
skill (`.continue/skills/platform_layers/SKILL.md`).

## The three components

| Component | Kind | Linked by | Provides |
|---|---|---|---|
| `hal_common` | header-only | everything | the shared vocabulary: types and concepts |
| `hal_board` | `.cpp` | the firmware (`mcgowan/`) | the real, GPIO-backed drivers |
| `hal_stub` | `.cpp` | the host tests, `donny/` | the pin-free, software-driven stubs |

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
`hal/push_button_types.hpp`) plus a per-implementation header of the same name
(e.g. `hal/push_button.hpp`) that includes it and adds the concrete class. For
the push button:

- `hal/push_button_types.hpp` (`hal_common`) defines
  `thermo::hal::ButtonState`, `thermo::hal::ButtonCallback`,
  `thermo::hal::ButtonHandler`, and the `thermo::hal::PushButtonLike` concept —
  the single definition of the shared types.
- `hal/push_button.hpp` (`hal_board` and `hal_stub`, one each) defines
  `thermo::hal::PushButton` with an identical public interface (same ctor,
  inline `create()` factory, `init()`, `read()`, `set_interrupt()`). Each
  `static_assert`s `PushButtonLike<PushButton>`, so the two implementations
  cannot drift from the shared contract (or from each other).

## The HAL is semantics-free

`PushButton::read()` returns `ButtonState::kOn` while the button is pressed and
`ButtonState::kOff` while it is released, and interpreting a press (edge
detection, debouncing, hold timing) is the caller's job. Every device follows
the same rule: the HAL moves bytes and reports state, it never decides what
that state means.

## The mode switch is not a HAL device

ESP-IDF has no direct notion of a DPDT switch, so the board models each of its
two mode-sense poles as an independent `PushButton` — `board.config_button()`
and `board.run_button()` — and the DPDT knowledge (both poles on is a hardware
fault, both off is the center/Off position, one of them selects Configure or
Run) lives in the logic layer. The stub's `PushButton::set()` simulates
presses, so bad hardware is reproducible in tests.

## The WiFi radio

The WiFi radio (§3.2, §3.9) follows the same header pair: `hal/wifi_types.hpp`
(`hal_common`) defines `AccessPointConfig`, `StationConfig`, `copy_text()`, the
length limits, and the `WifiRadioLike` concept; `hal/wifi.hpp` (`hal_board` /
`hal_stub`) defines `WifiRadioBoard` / `WifiRadioStub` and aliases
`using WifiRadio = ...`. The interface is `init()`, `start_ap()`/`stop_ap()`,
`start_station()`/`stop_station()` — the API and station are **independent**
(Configure mode may run both at once) and the radio is semantics-free (callers
own retry policy). An empty station SSID is rejected with
`ESP_ERR_INVALID_ARG`; callers must not start it. The stub adds observation
accessors (`initialized()`, `ap_running()`, `station_running()`, `ap_config()`,
`station_config()`), useful in tests.

## The one-shot timer

**The one-shot timer is a HAL device, but a new one.** `hal/timer_types.hpp`
(`hal_common`) defines the `TimerLike` concept (`cancel()`) and the
`OneShotTimerFactoryLike<Timer, TimerHandle>` concept
(`register_timer(milliseconds, std::function<void()>) -> TimerHandle`).
`register` cannot be the method name (it is a C++ keyword), hence
`register_timer`. The arming class is a *factory* rather than a timer: it holds
no timer of its own, creating one per call and handing it back through the
handle. The handle type is a concept parameter rather than a fixed type,
because the two implementations arm timers through different mechanisms and the
caller only needs `cancel()`. Cancellation is **racy by contract**: the
callback may already be running (or already be queued for the timer task) when
`cancel()` returns, so a caller that must not act on a stale callback guards
the callback body itself.

Unlike the other HAL devices there is no shared `hal/timer.hpp` and no single
`Timer` class: the two implementations are named after themselves, so a project
always states which one it linked, and board-independent code targets the
concept rather than a concrete type.

- **`hal_board`:** `hal/timer_board.hpp` + `timer_board.cpp` provide
  `OneShotTimerFactoryBoard` (the arming factory) and `TimerBoard` (the
  move-only handle). Both `static_assert` their concepts, so the implementation
  cannot drift from the shared contract. It is built on `esp_timer` with
  `ESP_TIMER_TASK` dispatch, so the callback runs in the esp_timer task and may
  use ordinary FreeRTOS APIs — but must stay short, since that one task also
  dispatches every other `esp_timer` callback. The handle **owns** the
  `esp_timer` instance (RAII): `cancel()` — also run by the destructor — arms
  nothing further, stops with `esp_timer_stop_blocking(…, portMAX_DELAY)`, then
  deletes the instance. The `std::function` lives in a `shared_ptr` the handle
  holds while the timer's `arg` points at it; `esp_timer_stop_blocking()` from
  task context does not return until no callback is running, which is what
  makes releasing the box safe. A handle destroyed from *inside* its own
  callback is the exception (the stop returns immediately there), so the
  callback copies the callable before invoking it and touches the box no
  further. Consequently `cancel()` on this implementation is **stronger** than
  the abstract contract: it is not racy when called from task context. A zero
  delay or a failed `esp_timer_create()` / `esp_timer_start_once()` trips an
  `assert` rather than silently dropping the request (after the assert it
  returns an empty handle, never a handle that owns no timer).
- **`hal_stub`:** `hal/timer_stub.hpp` + `timer_stub.cpp` provide
  `OneShotTimerFactoryStub` and `TimerStub`, likewise pinned by
  `static_assert`. It uses FreeRTOS software timers (`freertos/timers.h`)
  because the `linux` target has no `esp_timer` implementation —
  `test-hal-stub/main/test_freertos_timers.cpp` first pinned that the one-shot
  API works there, and `test-hal-stub/main/test_hal_timer_stub.cpp` covers the
  stub itself. Each `register_timer()` allocates one `TimerCallbackBox` (the
  caller's `std::function`) and creates a one-shot (`pdFALSE`) timer whose
  `pvTimerID` is that box, so it too is RAII-owned by a move-only handle.
  Lifetime is harder here than on the board, because FreeRTOS timer commands
  are **asynchronous**: start/stop/delete only queue work for the "Tmr Svc"
  task, so neither the timer nor its callback storage can be freed when
  `cancel()` returns. `cancel()` therefore queues the box's free with
  `xTimerPendFunctionCall()`, which FreeRTOS posts to the *same* command queue
  and so processes strictly after the timer's own delete — and the timer task
  drains expired timers before the queue, and the callback copies the callable
  before invoking it, so neither a late callback nor a self-cancel can read
  freed memory. Consequently `cancel()` on the stub is genuinely **racy**, as
  the abstract contract allows: a callback already dispatched may still run
  after `cancel()` returns. A delay that rounds to fewer than one tick (10 ms
  at `configTICK_RATE_HZ` = 100), a null callback, or a failed create/start
  trips an `assert`; after the assert an empty handle is returned.

## The aggregate board

**The aggregate board owns one of every device**, including the WiFi radio:
`BoardLike` (`hal/board_types.hpp`) requires `wifi()` (mutable and const), and
`init_all()` initializes the radio last. `init_all()` does **not** start the AP
or the station — the caller decides (Configure mode starts the AP, Run mode
starts the station). Reach the stub controls through the board, e.g.
`board.wifi().start_ap(cfg)` / `board.wifi().ap_running()`.

**The one-shot timer is on the board too, but not as a device accessor.**
`BoardLike` requires a public member typedef — `typename T::TimerHandle`, which
must satisfy `TimerLike` — plus a method
`T::TimerHandle register_timer(std::chrono::milliseconds, std::function<void()>)`.
Each board spells the typedef for the handle it hands back
(`Board::TimerHandle = TimerBoard`, `BoardStub::TimerHandle = TimerStub`) and
owns a private timer factory (`OneShotTimerFactoryBoard` /
`OneShotTimerFactoryStub`) that `register_timer()` delegates to. There
is **no** `timer()` accessor on purpose: a caller arms a timer and immediately
owns the handle, so exposing the factory as well would give it two ways to do
the same thing. Because the typedef is spelled by the implementation,
`BoardLike` stays implementation-free — it names only the `TimerLike` concept,
so board-independent code (templated on `hal::BoardLike`) can arm and cancel a
timer without knowing which mechanism is underneath.

The two timer factories are **movable** (defaulted move constructor, move
assignment still deleted, like every other HAL device). They hold no state —
each `register_timer()` creates its own timer — so moving one is
indistinguishable from copying it, and making them movable is what keeps
`Board` / `BoardStub` movable, which `create()` plus
`std::make_shared<Board>(Board::create({}))` relies on. `init_all()` does not
touch the timer: neither `esp_timer` nor the FreeRTOS timer service needs
per-board setup.

The two aggregate boards are **named differently on purpose**: `hal_board`'s is
`thermo::hal::Board` in `hal/board.hpp`, `hal_stub`'s is
`thermo::hal::BoardStub` in `hal/board_stub.hpp`. Nothing includes one header
hoping the other implementation is on the include path, so a project states
which board it linked. `hal_stub`'s devices keep the shared headers
(`hal/led.hpp`, `hal/wifi.hpp`, …), and the shared `WifiRadio` alias still
resolves per implementation. Code that must work with either takes the
`hal::BoardLike` template parameter and never names a concrete board — see
`logic/led_task.hpp`, whose LED-cycle table and coverage checks are free
functions (`thermo::ledtask::led_cycles()` / `has_cycle()`), so the
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
`BoardLike`, `TimerLike` and `OneShotTimerFactoryLike` concepts, and the
deep-sleep types.

- **`test-hal-common/`** covers the implementation-free `hal_common`
  vocabulary. `test_hal_board_types.cpp` pins the `BoardLike` contract from the
  other side: `BoardPins` defaults to the §2.4 pin map, `BoardWithoutTimer`
  spells out every requirement except the timer using the real stub devices,
  and `ModelBoard<Handle, Returned>` adds just the timer clause, so a model
  that names no handle, a non-class or non-cancelable handle, a
  `register_timer()` returning something other than the board's `TimerHandle`,
  or no `register_timer()` at all is each rejected for that reason alone.
  `test_hal_timer_types.cpp` models conforming and non-conforming timer/handle
  types to prove both concepts reject what they should, and
  `test_hal_sleep_types.cpp` pins the wake-cause vocabulary (`WakeCause` at
  ESP-IDF's bit positions, `wake_cause_bit()`, and `woke_from()`).
- **`test-hal-stub/`** covers the pin-free `hal_stub` devices. Each device's
  own file exercises its stub contract and its `hal_common` concept:
  `test_hal_led.cpp` (`LedLike`), `test_hal_push_button.cpp` (`PushButtonLike`,
  including the `set()` control the mode-sense poles use),
  `test_hal_onewire.cpp` (`OneWireLike` plus `set_sensors()`),
  `test_hal_chip_temperature.cpp` (`ChipTemperatureLike`), `test_hal_wifi.cpp`
  (`WifiRadioLike`, AP/station start/stop independence, and the empty-SSID
  rejection), and `test_hal_timer_stub.cpp` (`TimerStub` /
  `OneShotTimerFactoryStub`: firing exactly once, a captured callable really
  running, cancelling before and after the expiry, and move construction / move
  assignment transferring ownership). `test_freertos_timers.cpp` verifies that
  FreeRTOS software timers — what the stub timer is built on, since the `linux`
  target has no `esp_timer` — create, fire exactly once, re-arm, and stay
  silent after a cancel. `test_hal_board.cpp` covers the aggregate `BoardStub`:
  that `config_button()` / `run_button()` / `wifi()` are wired up, that
  `init_all()` initializes but does not start the radio, that
  `deep_sleep_start()` records the duration while `sleep_get_wakeup_causes()` /
  `set_wakeup_causes()` model the next boot's wake cause, and that the board
  names a cancelable `TimerHandle` and arms working timers through
  `register_timer()`.

Run them with `./scripts/dev.py test-hal-common` and
`./scripts/dev.py test-hal-stub`, or through `./scripts/dev.py test`, which
runs every suite.
