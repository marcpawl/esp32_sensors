# hal_board

Real, **GPIO-backed** implementations of the hardware abstraction layer.

This component provides the hardware drivers for the development board and is
linked by the **production firmware** (`main/`). It is one of two mutually
exclusive HAL implementations; a project must depend on exactly one of
`hal_board` or `hal_stub`.

## What lives here

| Header | Provides |
|---|---|
| `board.hpp` | `Board` — the aggregate owning every HAL device (chip temperature, 1-Wire bus, green/red LEDs, push button, mode switch, NTP client) |
| `switch.hpp` | `Switch` — the 3-position ON-OFF-ON mode switch |
| `led.hpp` | `Led` — a single LED |
| `push_button.hpp` | `PushButton` — a GPIO button with an interrupt callback |
| `onewire.hpp` | `OneWire` — the 1-Wire bus |
| `chip_temperature.hpp` | `ChipTemperature` — the internal temperature sensor |
| `network_time_protocol.hpp` | `NetworkTimeProtocol` — the SNTP-backed NTP client (queries a server for the current UTC time) |

Each device is implemented on top of the ESP-IDF GPIO / temperature-sensor
drivers (and `esp_sntp` for the NTP client), and `board.cpp` aggregates them
(`init_all()` initialises every device, returning the first error).

The shared device vocabulary (`PinState`, `Pins`, `SwitchLike`, `LedState`,
`LedLike`, `PushButtonLike`, `OneWireLike`, `ChipTemperatureLike`,
`NetworkTimeProtocolLike`, `BoardPins`, `BoardLike`) comes from the header-only
`hal_common` component; the pin assignments themselves come from `hal_common`'s
`pin_map.hpp`.

## Mutually exclusive with `hal_stub`

`hal_board` and `hal_stub` expose the **same** headers and define the same
symbols. Projects keep exactly one in view via `EXCLUDE_COMPONENTS` (set before
`project()`): the firmware excludes `hal_stub`; the simulated-environment and
host-test projects exclude `hal_board`.

When adding firmware that is *not* hardware-facing, keep it host-testable rather
than putting it here.
