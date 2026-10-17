# hal_stub

Pin-free, **software-driven stubs** implementing the hardware abstraction layer.

This component provides deterministic, hardware-free stand-ins for the HAL
devices, used to run the firmware's code in a **simulated environment** (the
native test project and the host unit tests). It is one of two mutually
exclusive HAL implementations; a project must depend on exactly one of
`hal_board` or `hal_stub`.

## What lives here

| Header | Provides |
|---|---|
| `board.hpp` | `Board` — the aggregate owning every (stubbed) HAL device |
| `switch.hpp` | `Switch` — software-settable mode switch; simulate bad hardware via `set_pins()` |
| `led.hpp` | `Led` — records its last-commanded state instead of driving a pin |
| `push_button.hpp` | `PushButton` — software-settable button whose callback fires on change |
| `onewire.hpp` | `OneWire` — scriptable bus responses |
| `chip_temperature.hpp` | `ChipTemperature` — a settable fake temperature |
| `network_time_protocol.hpp` | `NetworkTimeProtocol` — a settable fake time via `set_utc_epoch()`; simulate an unreachable server via `set_sync_fails()` |

Because the behavior is driven from software rather than a pin, tests can
exercise the same interface the firmware uses on real hardware — for example
`hal_stub`'s `Switch::set_pins()` lets a test simulate both-poles-on /
both-poles-off to check bad-hardware detection, and
`NetworkTimeProtocol::set_sync_fails()` lets a test exercise the NTP-failure
path (§5.3) without a network.

The shared device vocabulary comes from the header-only `hal_common` component;
the pin assignments come from `hal_common`'s `pin_map.hpp`.

## Mutually exclusive with `hal_board`

`hal_stub` and `hal_board` expose the **same** headers and define the same
symbols. Projects keep exactly one in view via `EXCLUDE_COMPONENTS` (set before
`project()`): the firmware excludes `hal_stub`; test projects running in the
simulated environment exclude `hal_board`.

## Building for the simulated environment

`hal_stub` is deliberately free of chip-specific GPIO driver calls, so it stays
within the simulated environment and is covered by `test/` (see
`test_hal_switch.cpp`, `test_hal_led.cpp`, `test_hal_push_button.cpp`,
`test_hal_onewire.cpp`, `test_hal_chip_temperature.cpp`,
`test_hal_network_time_protocol.cpp`, and `test_hal_board.cpp`). **Keep it that
way** when extending the stubs.
