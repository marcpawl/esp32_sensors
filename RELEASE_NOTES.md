Version 0.1
===========

Under development.

Implemented
-----------

- Project layout: the firmware is its own subproject (`mcgowan/`) producing
  `mcgowan.bin` for the ESP32-S3 (linking the `hal_board` HAL). The repo root is
  no longer an ESP-IDF project; it aggregates the subprojects (`mcgowan/`,
  `test/`, `espidf-cpp-test/`) with plain CMake, so one `scripts/dev.py build`
  builds them all.

- Configure mode: SoftAP, HTTPS server, configuration UI, NVS persistence,
  and LED semantics.
- Run mode (partial): boot self-test LEDs, battery read with hysteresis,
  sensor reads using cached ROM IDs (with a first-boot scan), sample append
  to the RTC-memory buffer, and deep sleep between samples. The RTC block's
  sample buffer and cached ROM IDs are `static_vector`s, so each carries its
  own element count instead of a separate `buffer_len` / `rom_count` counter
  that could drift out of step with the data (§8).
- Host-based unit tests for the pure-logic modules (`temp_format`, `payload`,
  `app_config` validation, `rom_id`, `rtc_buffer`) running on the ESP-IDF
  `linux` target.
- The mode-sense pole mapping (`logic/mode_selection.hpp`) is pure,
  `constexpr`, and ESP-IDF-free: it maps the two pole states to
  Configure / Off / Run and
  reports a both-poles-on miswire as `std::nullopt`, so
  `test_mode_selection.cpp` covers all four combinations on the host. The
  firmware's reboot-on-fault policy stays in `main.cpp`. `thermo::Mode` itself
  moved to the shared `logic` component (`logic/mode.hpp`), where it is
  board-independent vocabulary available to the firmware and the tests alike.
  The battery-hysteresis transmission gate `thermo::TxState` moved there too
  (`logic/led_manager.hpp`), so firmware code can name it without reaching
  into a board. The fixed capacities (`thermo::limits::kSensorCount`,
  `thermo::limits::kMaxBufferRecords`) moved as well (`logic/limits.hpp`), so
  the sample buffer, the payload, and the sensor layer share one definition
  with the host tests. The temperature formatter and its `kTempNull`
  sensor-failure sentinel moved there too (`logic/temp_format.hpp`, with a
  `src/temp_format.cpp` implementing the formatter), so the config UI, the
  payload, and the RTC sample defaults share one ESP-IDF-free definition.
- Unit tests for the pin-free `hal_stub` push button
  (`test_hal_push_button.cpp`), including the `set()` control the mode-sense
  poles use.
- A swappable hardware abstraction layer (`components/`): `hal_common` (shared
  types/concepts), `hal_board` (real GPIO drivers, linked by the firmware), and
  `hal_stub` (pin-free stubs, linked by the host tests). The 3-position mode
  switch is modelled as two independent push buttons (`config_button()` /
  `run_button()`) because ESP-IDF has no direct DPDT support; interpreting the
  pair is left to the logic layer.

- Deep sleep (§3.8) is a board-level operation rather than a HAL device, since
  it powers the whole board down: `BoardLike` requires
  `deep_sleep_start(std::chrono::seconds)` and `sleep_get_wakeup_causes()`.
  `hal_board` calls `esp_sleep_enable_timer_wakeup()` + `esp_deep_sleep_start()`
  and returns ESP-IDF's wake-cause bitmap; `hal_stub` records the requested
  duration (`last_deep_sleep()`), arms the timer wake the next boot would
  report, and returns, while `set_wakeup_causes()` lets a test model any other
  wake — so the sleep/backoff decision becomes host-testable. The shared
  vocabulary (`WakeCause`, `WakeCauses`,
  `wake_cause_bit()`, `woke_from()`) lives in `hal_common`'s
  `hal/sleep_types.hpp` and stays free of ESP-IDF dependencies, which the
  `linux` target requires: `esp_sleep.h` is not self-contained there and the
  port links no `sleep_modes.c`.

- Development environment: the dev container uses the **prebuilt, pinned**
  image `marcpawl/esp-idf:20261025` (official ESP-IDF v6.1 plus `gh`, PyGithub,
  and esp-clang). Developers and CI *pull* it instead of rebuilding it, so both
  use an identical toolchain and a CI run no longer spends minutes building an
  image. The tag is pinned in `docker-compose.yml` and `scripts/dev.py`
  (`DEV_IMAGE`); `scripts/dev.py self-check` fails if the two drift.
- The container runs as the **calling** user, so build artifacts are owned by
  whoever ran the command (no `chown`). For uids other than the image's 1000 —
  including GitHub's runner at 1001 — `scripts/dev.py` pins `HOME` and
  `CCACHE_DIR`, fixing the `ccache: error: Permission denied` CI failure.

Not yet implemented
-------------------

- Run mode network path: Wi-Fi station connect with cached fast reconnect,
  batch upload of the payload, and exponential backoff.
