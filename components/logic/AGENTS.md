# The application logic

This file documents `components/logic/` — the `liblogic` component and its host
test project. Repository-wide guidance (the build, the workflow, the quality
gates) is in the root `AGENTS.md`; the layering rules — which layer may require
which — are in the `platform_layers` skill
(`.opencode/skills/platform_layers/SKILL.md`).

## Layout

- `liblogic/` — the component. Public headers sit in
  `liblogic/include/logic/`, so every include names the `logic/` prefix; the
  compiled sources are in `liblogic/src/`.
- `tests/test-logic/` — the component's host test project: an ESP-IDF `linux`
  project built with Unity. It is its own project so it builds (and fails)
  independently of every other test suite.

The two live together here the way the HAL components and their tests live
together under `components/hal/`. ESP-IDF scans a component directory only one
level deep, so a project that requires `liblogic` must name
`components/logic` on its `EXTRA_COMPONENT_DIRS` even though `components/` is
already there; the test project computes its own entry from its depth in the
tree.

## The component

The headers are included with the `logic/` prefix:

| Header | Provides |
|---|---|
| `logic/led_manager.hpp` | `thermo::TxState` (`kTxOk` / `kTxInhibited`) — the §3.6 battery-hysteresis transmission gate; board-independent vocabulary, dependency-free (only `<cstdint>`) |
| `logic/limits.hpp` | `thermo::limits::kSensorCount` (3) and `thermo::limits::kMaxBufferRecords` (256) — the fixed capacities shared by the sample buffer, the payload, and the sensor layer (§2.1, §4.1, §4.2); dependency-free (only `<cstddef>`) |
| `logic/led_task.hpp` | `thermo::LedState`, `thermo::ledtask::*` (`Step`, `CycleSteps`, `LedRunner<Board>`), `thermo::LedTask<Board>` — the LED status task built on `espidf_cpp::Task` |
| `logic/mode.hpp` | `thermo::Mode` (`kOff` / `kConfigure` / `kRun`) and `thermo::to_string(Mode)` — the operating modes (§3), board-independent vocabulary |
| `logic/mode_selection.hpp` | `thermo::ModeSelection` (`std::optional<Mode>`) and `thermo::mode_from_buttons(ButtonState config, ButtonState run)` — the pure, `constexpr` mapping from the two mode-sense poles to a mode, reporting a both-poles-on miswire as `std::nullopt` |
| `logic/temp_format.hpp` / `src/temp_format.cpp` | `thermo::kTempNull` (the int16 `0x8000` sensor-failure sentinel) and `thermo::format_temp_c_x100()` — centred-hundredths formatting for the config UI and the payload (§4.2, §9); no ESP-IDF dependency |
| `logic/rom_id.hpp` / `src/rom_id.cpp` | `thermo::rom_id_to_hex()` and `thermo::rom_id_from_hex()` — the free functions that render a `thermo::hal::RomId` as the 16-character uppercase hex key of §5.1.3 / §6.1 and parse one back (`std::nullopt` on a wrong length or a non-hex character). Free functions, not members: `RomId` in `hal_common` stays pure data. No ESP-IDF dependency |
| `logic/payload.hpp` / `src/payload.cpp` | `thermo::payload::NamedSample` and `thermo::payload::build_batch_json()` — the §9.1 upload document (key names, ordering, `batch_size`, JSON escaping) built from the buffered records; the temperatures go through `logic/temp_format.hpp`. No ESP-IDF dependency |
| `logic/rtc_state.hpp` / `src/rtc_buffer.cpp` | `thermo::SampleRecord`, `thermo::WifiCache`, `thermo::RtcState` (the §8 retained block's *shape*) plus the buffer operations `rtc_buffer_push()` / `rtc_buffer_clear()`, the cold-boot `rtc_state_reset()`, and `rtc_wifi_cache_clear()` (§3.10, §4.2). Storage is **not** here: the `RTC_DATA_ATTR` block and the wake-cause policy are firmware-only (`mcgowan/src/rtc_storage.{hpp,cpp}`), because `RTC_DATA_ATTR` fails to compile on `linux` and the sleep API is unlinkable there |

`logic/mode.hpp`, `logic/led_manager.hpp`, `logic/limits.hpp`, and
`logic/temp_format.hpp` are deliberately dependency-free (only `<cstdint>` /
`<cstddef>`), so the firmware, the host tests, and `logic/mode_selection.hpp`
can all name the operating modes, the transmission gate, the fixed capacities,
and the temperature sentinel without pulling in a board.

### Dependencies

`liblogic` requires only lower layers: `libespidfcpp`, `hal_common`, and `sml`
(the wrapper around the `third_party/sml` submodule, which holds the state
machines for the operating modes and the run-mode cycle). FreeRTOS, logging,
and `esp_common` come from ESP-IDF's common requirements.

It must **never** require `hal_board` or `hal_stub`. Its board-facing code is
templated on a `hal_common` concept — `led_task.hpp` takes any
`hal::BoardLike` — and each project links exactly one implementation
(`EXCLUDE_COMPONENTS` picks it), so requiring either here would drag the other
into every project.

### Adding sources

`include/logic/` holds the public headers and `src/` the `.cpp` files
(`src/temp_format.cpp`, `src/rtc_buffer.cpp`, `src/self_signed_cert.cpp`,
`src/rom_id.cpp`). **New `.cpp` files must be added to the `SRCS` list in
`liblogic/CMakeLists.txt`**; adding sources there does not change any
consumer, because they all name the component.

## Board-independent templates

`LedRunner` and `LedTask` take a `std::shared_ptr<Board>` (never a null one,
where `Board` is the `hal::BoardLike` template parameter) and share the board
with the caller instead of owning it, so the caller can keep using the board
the task drives (e.g. a test reads back the LEDs). Create it with
`std::make_shared<BoardStub>(BoardStub::create(pins))` under `hal_stub`, or the
`thermo::hal::Board` equivalent under `hal_board`, and call `init_all()` before
starting the task. The HAL itself is unchanged: both boards stay non-copyable
and movable.

## The host tests

The test project compiles the component's `.cpp` files through
`PRIV_REQUIRES liblogic`, and directly compiles the driver-free firmware
sources under `mcgowan/src` (`app_config.cpp`, `config_page.cpp`,
`power_source.cpp`) so the tests exercise real code rather than copies.

`temp_format.cpp`, `rtc_buffer.cpp`, `rom_id.cpp`, and `payload.cpp` are
deliberately free of chip-specific ESP-IDF dependencies so they build for the
`linux` target. The header-only `logic/mode_selection.hpp` is the same idea
applied to the mode-sense poles: it depends only on
`hal/push_button_types.hpp` (for `ButtonState`) and `logic/mode.hpp`, and is
`constexpr`, so `test_mode_selection.cpp` exercises it directly and covers all
four pole combinations — including the both-on hardware fault, which the
header reports as `std::nullopt` instead of rebooting.

`test_led_runner.cpp` instantiates the templated `LedRunner<Board>` against the
stub `BoardStub`. The `hal_stub` component itself (the pin-free buttons, LEDs,
1-Wire bus, chip-temperature sensor, WiFi radio, one-shot timer, and aggregate
board) is covered by `components/hal/tests/test-hal-stub/`.

**Keep it that way** — put pure logic in these files and hardware/IDF calls
behind them.

Build and run the suite with `scripts/dev.py test-logic`, or directly in the
container:

```bash
scripts/dev.py shell -C components/logic/tests/test-logic \
    idf.py -B build_linux --preview set-target linux
scripts/dev.py shell -C components/logic/tests/test-logic \
    idf.py -B build_linux build
./components/logic/tests/test-logic/build_linux/test-logic.elf
```
