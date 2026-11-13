# The mcgowan firmware

`mcgowan/` is the ESP32-S3 firmware subproject (`mcgowan.bin`). Repository-wide
guidance (the build, the workflow, the quality gates) is in the root
`AGENTS.md`; the HAL and component details are in their own `AGENTS.md` files.

## `main/` vs `src/`

ESP-IDF requires the component that defines `app_main` to be named `main`, so
`mcgowan/main/` is a near-empty component that compiles `mcgowan/src/` and
exposes `mcgowan/src/include`. Application code lives in `mcgowan/src/`.

New firmware sources must be added to `mcgowan/main/CMakeLists.txt` or they
will not compile. Board-independent logic belongs in `components/logic/liblogic`
instead.

## `src/` module map

| Area | Files |
|---|---|
| Entry point | `main.cpp` (`extern "C" void app_main`), `constants.hpp` |
| Mode switching | `main.cpp` (`read_mode()` reads the poles and applies the reboot-on-fault policy); mapping in `logic/mode_selection.hpp` |
| Sensors | `onewire_bus.cpp` (bit-banged 1-Wire), `ds18b20.cpp`, `sensor_manager.cpp`, `sensor.hpp` |
| RTC state / buffer | `rtc_storage.cpp` (the `RTC_DATA_ATTR` block + `rtc_state()`) and `rtc_storage.hpp` (`rtc_state_init(board)`). The block shape and operations live in `logic/rtc_state.hpp` / `logic/src/rtc_buffer.cpp` |
| Power / battery | `battery_monitor.cpp`, `power_manager.cpp`, `power_source.cpp` |
| Config | `app_config.cpp` (validation), `app_config_store.cpp` (NVS) |
| Configure mode | `configure_mode.cpp`, `wifi_ap.cpp`, `https_server.cpp`, `config_page.cpp`; certificate in `logic/self_signed_cert.hpp` |
| Run mode | `run_mode.cpp` (stubbed network path), `ntp_client.cpp` |
| Payload | `payload.cpp`; temperature formatting in `logic/temp_format.hpp`, ROM-ID hex in `logic/rom_id.hpp` |

The LED task and the operating modes are board-independent and live in
`liblogic` (`logic/led_task.hpp`, `logic/mode.hpp`,
`logic/mode_selection.hpp`), not in `mcgowan/src/`.

## Configuration

`mcgowan/sdkconfig.defaults` is the committed configuration intent (target,
C++ exceptions/RTTI off, partition table, `-Os`). `mcgowan/sdkconfig` is
generated and untracked.
