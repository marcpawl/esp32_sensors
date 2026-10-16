# hal_common

Shared, **implementation-free** vocabulary for the hardware abstraction layer.

This header-only component defines the types and concepts that are common to
every HAL device implementation, so the shared definitions live in exactly one
place and the concrete implementations cannot drift apart.

## What lives here

| Header | Defines |
|---|---|
| `board_types.hpp` | `BoardPins` (GPIO assignments) and the `BoardLike` concept |
| `switch_types.hpp` | `PinState`, `Pins`, and the `SwitchLike` concept (3-position mode switch) |
| `led_types.hpp` | `LedState` and the `LedLike` concept |
| `push_button_types.hpp` | `ButtonState`, `ButtonCallback`, `ButtonHandler`, and the `PushButtonLike` concept |
| `onewire_types.hpp` | the `OneWireLike` concept (1-Wire bus) |
| `chip_temperature_types.hpp` | the `ChipTemperatureLike` concept (internal temperature sensor) |
| `ds18b20_types.hpp` | DS18B20 types shared with the sensor layer |
| `pin_map.hpp` | `thermo::pins::*` — the single source of truth for the reference-board GPIO pinout |

## Why a separate component

The HAL is split into one shared vocabulary plus two **mutually exclusive**
implementations:

- `hal_board` — real, GPIO-backed drivers (linked by the production firmware).
- `hal_stub` — pin-free, software-driven stubs (linked by the QEMU project and
  the host tests).

`hal_common` is **implementation-free**: it defines the vocabulary but no
concrete device class, so both implementations can require it without
duplicating definitions. It is shared and never excluded.

`esp_common` is required only for `esp_err.h` (the `init()` return type named by
some concepts). No driver is pulled in.
