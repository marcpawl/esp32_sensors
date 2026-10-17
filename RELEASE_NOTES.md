Version 0.1
===========

Under development.

Implemented
-----------

- Configure mode: SoftAP, HTTPS server, configuration UI, NVS persistence,
  and LED semantics.
- Run mode (partial): boot self-test LEDs, battery read with hysteresis,
  sensor reads using cached ROM IDs (with a first-boot scan), sample append
  to the RTC-memory buffer, and deep sleep between samples.
- Host-based unit tests for the pure-logic modules (`temp_format`, `payload`,
  `app_config` validation, `rom_id`, `rtc_buffer`) running on the ESP-IDF
  `linux` target.
- Unit tests for the pin-free `hal_stub` mode switch (`test_hal_switch.cpp`):
  construction/`init()`, the default reading, `set_pins()` round-trips, bad
  hardware simulation, and the `SwitchLike` contract.
- Native `espidf_cpp_test` Unity tests (`espidf_cpp_test/`) that build and run
  natively for the ESP-IDF `linux` target: FreeRTOS-backed `thermo::Task` tests
  (`test_task.cpp`) plus HAL-stub linkage checks (`test_switch.cpp`,
  `test_led.cpp`, `test_push_button.cpp`, `test_onewire.cpp`,
  `test_chip_temperature.cpp`, `test_board.cpp`).
- A swappable hardware abstraction layer (`components/`): `hal_common` (shared
  types/concepts), `hal_board` (real GPIO drivers, linked by the firmware), and
  `hal_stub` (pin-free stubs, linked by the simulated environment and the host
  tests).
- An `espidf_cpp` component (`components/espidf_cpp`) of header-only C++
  wrappers over ESP-IDF/FreeRTOS C APIs (`Task`, `OneMessageQueue`, handle
  deleters, and the fixed-capacity `static_vector`/`static_string` containers).

Not yet implemented
-------------------

- Run mode network path: Wi-Fi station connect with cached fast reconnect,
  batch upload of the payload, and exponential backoff.
