# Repository Overview

## Project Description

This repository contains an **ESP32 Solar-Powered Thermometer Logger** and its software specification. Firmware implementation is **in progress**: Configure mode is fully implemented; Run mode is scaffolded with the network paths (Wi-Fi station, batch upload, backoff) intentionally stubbed.

**What it does:**
- A solar-powered, battery-backed ESP32 samples three **DS18B20** thermometers on a shared **1-Wire bus**.
- Readings are uploaded to a remote URL over **WiFi** at a configurable cadence.
- A **3-position ON-OFF-ON switch** selects the operating mode: **Configure / Off / Run**.
- Between samples the device enters **deep sleep** to minimize power draw.
- The sample buffer and WiFi connection cache live in **RTC memory** so they survive deep sleep and backoff reboots.

**Main purpose and goals:**
- Long-duration, low-power remote temperature monitoring.
- Robust logging that survives power cycles and connectivity loss.
- Configurable, field-servicable behavior via a local HTTPS configuration server.

**Key technologies:**

| Item | Value |
|---|---|
| Host OS | Linux Mint |
| IDE | CLion 2026.2 |
| Container runtime | Docker |
| Container image | `marcpawl/esp-idf:20261109b` (prebuilt: `espressif/idf:release-v6.1` + project tooling) |
| Source language | **C++26** (GNU extensions, `-std=gnu++26`) |
| Target framework | ESP-IDF v6.1 |
| Toolchain | GCC 15.2.0 (Xtensa `esp-15.2.0_20251204`, bundled with ESP-IDF v6.1) |
| License | GPL-3.0 |

> **Note:** Firmware source lives in the `mcgowan/` subproject and builds successfully (`mcgowan.bin` has been produced). `Specification.md` remains the authoritative source of truth for all implementation work; where code and spec disagree, the spec wins. The `install.sh` at the repo root is an unrelated Continue CLI installer artifact and is **not** part of the firmware.

> **Implementation status (current milestone):**
> - **Configure mode — implemented.** SoftAP, HTTPS server, config UI, NVS persistence, LED semantics.
> - **Run mode — scaffolded/stubbed.** Real: boot self-test LEDs, battery read + hysteresis, sensor read with cached ROM IDs (first-boot scan), sample append to the RTC buffer, deep sleep. **Stubbed:** Wi-Fi station connect + cached fast-reconnect (§3.9), batch upload + payload wire-out (§9, payload is formatted and logged but not sent), exponential backoff (§3.7).
> - **Host unit tests.** `test-hal-stub/`, `test-hal-common/`, `test-logic/`, and `test-espidf-cpp/` build for the ESP-IDF `linux` target (Unity) and run via `scripts/dev.py test`, which runs all four suites (`test-hal-common` first and `test-hal-stub` next; `test-hal-common`, `test-hal-stub`, `test-logic`, and `test-espidf-cpp` run one suite each). On-target tests for hardware-dependent code are planned.

> **Toolchain note:** The host's locally installed ESP-IDF is **v6.1** with **Xtensa GCC 15.2.0** (`esp-15.2.0_20251204`), which matches the pinned Docker image `marcpawl/esp-idf:20261109b` exactly (it is built on `espressif/idf:release-v6.1`). Keep both in sync: if you upgrade the host toolchain, change the base image in `Dockerfile`, publish a new image under a **new** date-stamped tag (never move an existing tag), and bump the pin in `docker-compose.yml` + `scripts/dev.py` (`DEV_IMAGE`) and this document.

## Architecture Overview

The intended system architecture (per `Specification.md`) is a single ESP-IDF firmware application running on an ESP32 with the following subsystems:

- **Sensor layer** — 1-Wire bus driver reading three DS18B20 temperature sensors.
- **Mode controller** — reads the two mode-sense poles of the 3-position switch (`config_button()` / `run_button()`) to select Configure / Off / Run.
- **Sample buffer** — in **RTC memory only**; accumulates readings across deep-sleep cycles.
- **WiFi / uploader** — connects to WiFi, uploads batched payloads to a remote URL, with connection caching and backoff.
- **Power manager** — drives deep sleep between samples to meet the hourly power budget.
- **Configuration** — persisted settings managed via an HTTPS configuration server (Configure mode).
- **LED semantics** — status indication for the various operating states.

**Data flow (Run mode):**
1. Wake from deep sleep → read three DS18B20 sensors over the shared 1-Wire bus.
2. Append readings to the RTC-memory sample buffer.
3. At the configured cadence, connect to WiFi (reusing cached connection info) and upload the batched payload to the remote URL.
4. On failure, apply backoff (state survives reboot via RTC memory).
5. Return to deep sleep.

**Data flow (Configure mode):**
1. Device starts an HTTPS server.
2. Operator connects to read/update configuration over HTTPS.
3. Configuration persists for subsequent Run-mode cycles.

Refer to `Specification.md` for the detailed contracts: operating modes, sample buffer semantics, configuration keys, HTTPS server behavior, LED semantics, RTC memory layout, payload format, and the power budget.

## Directory Structure

```
esp32_sensors/
├── Specification.md     # Authoritative software spec — read this first
├── CMakeLists.txt       # Root aggregator (plain CMake) — builds all subprojects
├── mcgowan/             # Firmware subproject (ESP32-S3) -> mcgowan.bin
│   ├── CMakeLists.txt   #   standalone ESP-IDF project (project(mcgowan))
│   ├── sdkconfig.defaults
│   ├── main/            #   ESP-IDF `main` component (must be named main)
│   │   └── CMakeLists.txt
│   └── src/             #   C++26 sources + include/ — see module map below
├── components/          # Reusable ESP-IDF components (see "HAL" section)
│   ├── espidf_cpp/      # C++ wrappers over ESP-IDF/FreeRTOS (Task, queue, handles, ...)
│   ├── logic/           # Board-independent application logic (temp_format, rom_id, payload, limits, led_manager, mode*, led_task, self_signed_cert)
│   ├── hal_common/      # Shared, header-only HAL vocabulary (types/concepts)
│   ├── hal_board/       # Real, GPIO-backed HAL
│   ├── hal_stub/        # Pin-free HAL stub
│   └── sml/             # Wrapper component exposing the third_party/sml submodule
├── third_party/         # Vendored third-party code (never formatted, never tidy'd)
│   ├── json/            #   nlohmann/json v3.11.3 (header-only, MIT)
│   └── sml/             #   boost-ext/sml git SUBMODULE, pinned to v1.2.0
├── test-hal-stub/       # Host tests for components/hal_stub (IDF `linux`, Unity)
├── test-hal-common/     # Host tests for components/hal_common (IDF `linux`, Unity)
├── test-logic/          # Host-based unit tests subproject (IDF `linux`, Unity)
├── test-espidf-cpp/     # Host tests for components/espidf_cpp (IDF `linux`, Unity)
├── donny/               # Linux-emulated application (IDF `linux`)
│   └── main/            #   app_main + the JSON console task (test driver)
├── Dockerfile           # Dev container: ESP-IDF v6.1 + non-root host-mapped user
├── docker-compose.yml   # `dev` service with serial (/dev) passthrough
├── .devcontainer/       # CLion / VS Code Dev Container definition (reuses compose)
├── .continue/           # Continue CLI config: `rules/` (slash commands), `skills/`
│   └── skills/          #   one SKILL.md per topic: cpp-coding-standards, git,
│                        #   platform_layers, quality_checks
├── scripts/dev.py       # Container task runner (build/flash/test/format/shell)
├── scripts/idf.sh       # Runs idf.py with the ESP-IDF env, sourcing export.sh if absent
├── scripts/test_dev.py  # Host unit tests for dev.py (`dev.py self-check`)
├── scripts/dev.py-completion.bash  # Bash tab-completion for scripts/dev.py
├── build_root/          # Root aggregator CMake build tree (git-ignored)
├── */cmake-build-*/     # CLion's own per-subproject build trees (git-ignored; see .idea/cmake.xml)
├── .dockerignore        # Build-context exclusions
├── .gitignore           # Excludes build dirs, sdkconfig, local IDE state
├── AGENTS.md            # This file — guidance for AI agents and developers
├── LICENSE              # GNU General Public License v3.0
├── install.sh           # Unrelated Continue CLI installer (not firmware)
└── .idea/               # CLion config (shared cmake.xml + run configs tracked; workspace.xml local)
```

**The root is not an ESP-IDF project.** The top-level `CMakeLists.txt` is plain
CMake that only aggregates the subprojects, so a single build covers everything.
Each subproject is a standalone ESP-IDF project with its own `CMakeLists.txt`,
`project()`, `sdkconfig.defaults`, and a build directory **inside its own
folder**, named after the target:

| Subproject | Directory | Target | Produces |
|---|---|---|---|
| `mcgowan/` (firmware) | `mcgowan/build_esp32s3/` | esp32s3 | `mcgowan.bin` (flashed to the board) |
| `test-hal-stub/` (HAL stub device tests) | `test-hal-stub/build_linux/` | linux | `esp32_sensors_hal_stub_tests.elf` (run on the host) |
| `test-hal-common/` (HAL vocabulary tests) | `test-hal-common/build_linux/` | linux | `esp32_sensors_hal_common_tests.elf` (run on the host) |
| `test-logic/` (host tests) | `test-logic/build_linux/` | linux | `test-logic.elf` (run on the host) |
| `test-espidf-cpp/` (wrapper tests) | `test-espidf-cpp/build_linux/` | linux | `test-espidf-cpp.elf` (run on the host) |
| `donny/` (linux app) | `donny/build_linux/` | linux | `donny.elf` (an application, not a test: it runs the JSON console task and `Application`) |

The aggregate (root) project configures into `build_root/`. Each aggregate
target is prefixed with its subproject name (`mcgowan_build`,
`test-hal-stub_build`, `test-hal-common_build`, `test_build`,
`test-espidf-cpp_build`, `donny_build`) so it cannot collide with another
target in the project, with names CMake/ESP-IDF define themselves (e.g.
`test`), or with a directory name. `scripts/dev.py build` drives the aggregate
project; `scripts/dev.py build mcgowan` builds just the firmware. `mcgowan/`
targets ESP32-S3; `test-hal-stub/`, `test-hal-common/`, `test-logic/`,
`test-espidf-cpp/`, and `donny/` target `linux`. All build trees are
git-ignored.

**Why `mcgowan/main/` and `mcgowan/src/`.** ESP-IDF requires the component that
defines `app_main` to be named `main`, so `mcgowan/main/` is that (near-empty)
component; it compiles the sources from `mcgowan/src/` and exposes
`mcgowan/src/include`. Application code lives in `mcgowan/src/`.

**Key files:**
- `Specification.md` — the single source of truth for hardware, modes, RTC memory layout, payload format, and power budget. Any implementation must conform to it.
- `mcgowan/main/CMakeLists.txt` — the firmware's component definition. **Add new sources here** or they will not compile. Board-independent logic belongs in `components/logic` instead.
- `mcgowan/sdkconfig.defaults` — the committed configuration intent (target, C++ exceptions/RTTI off, partition table, `-Os`). `mcgowan/sdkconfig` is generated from this and is **not** tracked.
- `CMakeLists.txt` (root) — the generator-agnostic aggregator: one prefixed target per subproject invoking `idf.py -C <sub> -B <sub>/<build-dir>`. `-DSUBPROJECTS="mcgowan;test-hal-stub;test-hal-common;test-logic;test-espidf-cpp;donny"` (default: all) selects what is built.
- `Dockerfile` / `docker-compose.yml` — the reproducible ESP-IDF build/flash environment. Extend these (not ad-hoc `docker run`) when adding tooling. The compose `dev` service is also the target of the Dev Container (see below).
- `.devcontainer/devcontainer.json` — the CLion / VS Code **Dev Container** definition. It **reuses the compose `dev` service** (it does not define its own image), so CLion pulls/starts the same container the CLI uses. Edit `docker-compose.yml` for image/mount/user changes; edit `devcontainer.json` only for IDE-level wiring.
- `scripts/dev.py` — the single container task runner (`pull`, `image`, `submodules`, `build`, `flash`, `monitor`, `menuconfig`, `test`, `test-hal-common`, `test-hal-stub`, `test-logic`, `test-espidf-cpp`, `self-check`, `format`, `format-check`, `format-check-staged`, `pycodestyle`, `clang-tidy`, `install-hooks`, `shell`, `cn`). CLion run configurations and the docs both call it, so `docker compose` specifics live in one place. `submodules` fetches the pinned git submodules (`third_party/sml`); `build`, `test`, and `clang-tidy` call the same helper first, so a bare clone builds without a manual step. `shell` is the preferred way to run a container command from the host: with no arguments it opens an interactive login shell, and with arguments it runs them in the container (e.g. `./scripts/dev.py shell ls -1d /`). `cn` runs the Continue CLI in the container: it passes `--config <container HOME>/.continue/config.yaml` explicitly (cn's own default lookup is not reliable), and forwards its arguments to `cn` verbatim (`./scripts/dev.py cn -p "prompt"`). The pre-commit hook (`.githooks/pre-commit`) runs the quality gates; see the `quality_checks` skill. `pull`, `image`, and `self-check` are host-only — the tasks whose *decisions* only exist on the host — so each refuses to run in the container with an explanatory message. An internal `complete` command lists subcommands matching a prefix (used only by the completion script).
- `scripts/idf.sh` — runs a command (normally `idf.py`) with the ESP-IDF environment established, sourcing `$IDF_PATH/export.sh` when `IDF_PYTHON_ENV_PATH`/`ESP_IDF_VERSION` are absent. The image's entrypoint sources `export.sh` for the container's own command only; anything started later — notably the CMake/Ninja build CLion spawns — inherits a bare environment, where `idf.py` dies with `TypeError: expected string or bytes-like object, got 'NoneType'` (`Version.coerce(os.getenv('ESP_IDF_VERSION'))`). So the root aggregator's targets invoke `idf.py` through this wrapper (`set(IDF_RUNNER ...)` in `CMakeLists.txt`), which makes `cmake --build build_root` correct however it was started. `BASH_ENV` cannot substitute: bash sources it only for non-interactive shells and the image's `~/.bashrc` returns early for those. The wrapper is idempotent — it execs the command directly when the variables are already present — and exits non-zero when `IDF_PATH` is not an ESP-IDF checkout.
- `scripts/test_dev.py` — host-side unit tests for `dev.py` itself (stdlib `unittest`, run by `scripts/dev.py self-check`). Imports `dev.py` in-process and covers its pure helpers — chiefly the command line `shell` builds (so shell syntax like pipes/semicolons is passed through verbatim) and `shell -C` path resolution. `SourceFileDiscoveryTests` pins that `format`/`format-check` see untracked sources: `_source_file_command` enumerates `--cached --others --exclude-standard` scoped to `ROOTS`, `_select_sources` keeps only the C/C++ extensions and drops paths that are not on disk, and end-to-end checks against a scratch repository prove an untracked source is listed while a `.gitignore`d build file is not (the bug where `format` skipped files that had not been `git add`ed). `StagedFormatCheckTests` pins `format-check-staged`'s style handling against throwaway git repositories: the ancestor search that finds nested `.clang-format` / `.clang-format-ignore` files, that the *staged* blob (not the working copy) is materialized, that a style file staged for deletion is skipped while an untracked one is honored — plus two end-to-end checks that a nested `DisableFormat: true` exempts unformatted code while the root style still rejects it (they skip on the host, where `clang-format` is not installed). It also pins the `scripts/idf.sh` contract (`IdfRunnerTests`): the wrapper exists, is executable, is tracked by git, establishes the environment, passes the command through unmodified, and is used by the root aggregator's per-subproject helper. `ClionRunConfigurationTests` pins the four CMake run configurations to the CMake project they are actually bound to: `PROJECT_NAME` must be the root `project()` name, `CONFIG_NAME` must name a profile declared in `.idea/cmake.xml`, and `TARGET_NAME` must be one of the root aggregator's `*_build` targets (a subproject's `<name>.elf` is not resolvable from the root and fails with "cannot find target"). `HostTestCommandTests` pins the four host suites: the `test-hal-common`, `test-hal-stub`, `test-logic`, and `test-espidf-cpp` commands each build and run only their own project, and the aggregate `test` runs every `HOST_TEST_PROJECTS` entry in table order with `test-hal-common` first and `test-hal-stub` next, stopping at the first failing suite. The `Tidy*` classes cover `clang-tidy`: `TidyCompileArgumentsTests` pins which GCC flags are stripped and that the source file and the `-D`/`-I` flags survive; `TidyUnitSelectionTests` pins the one-entry-per-source reduction (non-C++ and out-of-tree entries skipped, first occurrence wins); `TidyConfigurationTests` pins the project list, the build directories it reads, and the header filter; and `TidyGateTests` pins the hook and CI ordering (tidy before format-check, tidy after build). `SubmoduleTests` pins the `third_party/sml` git submodule: `.gitmodules` records the upstream URL, the **index** records a `160000` gitlink at the v1.2.0 commit (the index, not `HEAD`, because the submodule is only staged until it is committed), every entry in `dev.SUBMODULES` has a marker file inside it, `_submodule_update_command` builds the `git submodule update --init --recursive -- <path>` argv, the fetch helper is called by `build` / `build-test-espidf-cpp` / `test` / `clang-tidy`, `submodules` runs through the container, and CI fetches submodules **before** it builds. It also pins that `third_party/` is outside `dev.ROOTS` and that `_is_vendored` excludes the submodule from `clang-tidy` while leaving `components/sml` and `components/logic` in scope. `ContinueCliCommandTests` pins `dev.py cn`: `_cn_command` puts `--config` first with the path under the container `$HOME` that `_container_home` picks (so it matches a compose `~/.continue` mount target for both the image user and the pinned `/tmp` home), arguments after `cn` are forwarded verbatim, `main` bypasses argparse for them, and the compose `environment:` passes `DEEPSEEK_API_KEY` through from the host with no value (never hardcoded). `PythonSourceDiscoveryTests` pins the `pycodestyle` file list: `_is_vendored_path` matches only a top-level `third_party`, `_select_python_sources` keeps `*.py` **and** extensionless Python shebang scripts (so `.githooks/pre-commit` is checked), rejects a non-Python shebang, a `python` mention that is not a shebang, deleted paths and non-UTF-8 first lines, and that the real repository enumerates exactly the two dev.py scripts and the extensionless hook, with nothing vendored. `PycodestyleGateTests` pins the wiring: `_pycodestyle_command` uses `python3 -m pycodestyle` with no options of its own (so a bare `pycodestyle` agrees), the command hops into the container, and it is the **first** gate in the hook and runs before `build` in CI. `SelfCheckModuleTests` pins the `self-check` command itself and, in particular, that it is **host-only**: inside the container none of the decisions the suite pins ever happen (a command never re-execs itself), so the run would pass vacuously — `cmd_self_check()` therefore returns 1 with a message naming `./scripts/dev.py self-check` for the host and `./scripts/dev.py test` for the container's own suites, never runs the suite there, never calls `change_to_container()`, and on the host runs exactly `python3 -m unittest -v scripts/test_dev.py`.
- `scripts/dev.py-completion.bash` — bash tab-completion for `scripts/dev.py`. Source it to complete subcommands (e.g. `./scripts/dev.py form<TAB>` → `format`, `format-check`, `format-check-staged`, or `./scripts/dev.py clang-<TAB>` → `clang-tidy`). It asks `dev.py complete <prefix>` for candidates, so the command list is defined in exactly one place.
- `.idea/cmake.xml` / `.idea/runConfigurations/*.xml` — **committed** shared CLion configuration. `cmake.xml` defines one CMake profile per subproject, so each subproject is indexed and buildable inside the Dev Container; the run configurations drive the build, test, format, flash, and shell tasks (six via `python3 scripts/dev.py`, four native CMake configs — one per profile — for build/refactor). `.idea/workspace.xml` (local, git-ignored) holds per-machine state. **Known limitation:** those profiles do not yet give the code model they were designed for. `GENERATION_DIR` is only the build directory; the source directory comes from the single CMake workspace in `.idea/misc.xml` (the repo root), so all four `cmake-build-*` trees configure the root aggregator (`CMAKE_HOME_DIRECTORY=/project`), contain no `compile_commands.json`, and expose only the four aggregate targets as `UTILITY` targets. Navigation/refactorings are therefore not driven by them; `scripts/dev.py build` is the way to build everything. Fixing it needs either one CLion project per subproject or a merged `compile_commands.json` at the root. **CLion rewrites these XML files from its in-memory model**: it drops XML comments and the trailing newline, and it restores the real `PROJECT_NAME` (`esp32_sensors_subprojects`, the root `project()` name) whenever it has resolved the project. Do not commit its wholesale rewrite — keep the comments and edit only the attribute that genuinely changed.
- `LICENSE` — GPL-3.0.

**`mcgowan/src/` module map:**

| Area | Files |
|---|---|
| Entry point | `main.cpp` (`extern "C" void app_main`), `constants.hpp` |
| Mode switching | `main.cpp` (`read_mode()` reads the poles and applies the reboot-on-fault policy); the mapping itself is `logic/mode_selection.hpp` |
| Sensors | `onewire_bus.cpp` (bit-banged 1-Wire), `ds18b20.cpp`, `sensor_manager.cpp`, `sensor.hpp` |
| RTC state / buffer | `rtc_storage.cpp` (the `RTC_DATA_ATTR` block + `rtc_state()`) and `rtc_storage.hpp` (`rtc_state_init(board)`, which asks the board's wake cause whether the retained block survives). The block's *shape*, the FIFO, the cold-boot reset and the Wi-Fi-cache clear are board-independent and live in `logic/rtc_state.hpp` / `logic/src/rtc_buffer.cpp`; `RtcState`'s `buffer` / `rom_ids` are `espidf_cpp::static_vector`s that carry their own counts |
| Power / battery | `battery_monitor.cpp`, `power_manager.cpp`, `power_source.cpp` |
| Config | `app_config.cpp` (validation), `app_config_store.cpp` (NVS) |
| Configure mode | `configure_mode.cpp`, `wifi_ap.cpp`, `https_server.cpp`, `config_page.cpp`; the self-signed certificate is `logic/self_signed_cert.hpp` |
| Run mode | `run_mode.cpp` (stubbed network path), `ntp_client.cpp` |
| Payload | `payload.cpp`; the temperature formatter and `kTempNull` are `logic/temp_format.hpp`, and ROM-ID hex is `logic/rom_id.hpp` |

The LED task (`led_task.hpp`) and the operating modes and their pole mapping
(`mode.hpp`, `mode_selection.hpp`) are **not** in `mcgowan/src/`: they are
board-independent logic and live in the `logic` component (see below),
included as `logic/led_task.hpp`, `logic/mode.hpp` and
`logic/mode_selection.hpp`.

**HAL components (`components/`).** Hardware-specific drivers live in reusable
ESP-IDF components rather than in `mcgowan/src/`, so host tests can swap them
for deterministic stubs. The components, their layers, and the platform rules
are documented in the `platform_layers` skill
(`.continue/skills/platform_layers/SKILL.md`). The vocabulary's own tests —
`BoardLike`, `TimerLike`, `OneShotTimerFactoryLike`, and the deep-sleep types —
live in `test-hal-common/`, while the stub devices' tests live in
`test-hal-stub/` (the hardware-backed devices are covered by on-target tests).

The interface for each HAL device is a header pair: the shared vocabulary in
`hal_common` (e.g. `hal/push_button_types.hpp`) plus a per-implementation header
of the same name (e.g. `hal/push_button.hpp`) that includes it and adds the
concrete class. For the push button:

- `hal/push_button_types.hpp` (`hal_common`) defines
  `thermo::hal::ButtonState`, `thermo::hal::ButtonCallback`,
  `thermo::hal::ButtonHandler`, and the `thermo::hal::PushButtonLike` concept —
  the single definition of the shared types.
- `hal/push_button.hpp` (`hal_board` and `hal_stub`, one each) defines
  `thermo::hal::PushButton` with an identical public interface (same ctor, inline
  `create()` factory, `init()`, `read()`, `set_interrupt()`). Each `static_assert`s
  `PushButtonLike<PushButton>`, so the two implementations cannot drift from the
  shared contract (or from each other).

The HAL is **semantics-free**: `PushButton::read()` returns `ButtonState::kOn`
while the button is pressed and `kOff` while it is released, and interpreting a
press (edge detection, debouncing, hold timing) is the caller's job.

**The mode switch is not a HAL device.** ESP-IDF has no direct notion of a
DPDT switch, so the board models each of its two mode-sense poles as an
independent `PushButton` — `board.config_button()` and `board.run_button()` —
and the DPDT knowledge (both poles on is a hardware fault, both off is the
center/Off position, one of them selects Configure or Run) lives in the logic
layer. The stub's `PushButton::set()` simulates presses, so bad hardware is
reproducible in tests.

The WiFi radio (§3.2, §3.9) follows the same header pair: `hal/wifi_types.hpp`
(`hal_common`) defines `AccessPointConfig`, `StationConfig`,
`copy_text()`, the length limits, and the `WifiRadioLike` concept;
`hal/wifi.hpp` (`hal_board` / `hal_stub`) defines `WifiRadioBoard` /
`WifiRadioStub` and aliases `using WifiRadio = ...`. The interface is
`init()`, `start_ap()`/`stop_ap()`, `start_station()`/`stop_station()` — the
API and station are **independent** (Configure mode may run both at once) and
the radio is semantics-free (callers own retry policy). An empty station SSID
is rejected with `ESP_ERR_INVALID_ARG`; callers must not start it. The stub
adds observation accessors (`initialized()`, `ap_running()`,
`station_running()`, `ap_config()`, `station_config()`), useful in tests.

**The one-shot timer is a HAL device, but a new one.** `hal/timer_types.hpp`
(`hal_common`) defines the `TimerLike` concept (`cancel()`) and the
`OneShotTimerFactoryLike<Timer, TimerHandle>` concept
(`register_timer(milliseconds, std::function<void()>) -> TimerHandle`).
`register` cannot be the method name
(it is a C++ keyword), hence `register_timer`. The arming class is a *factory*
rather than a timer: it holds no timer of its own, creating one per call and
handing it back through the handle. The handle type is a concept
parameter rather than a fixed type, because the two implementations arm timers
through different mechanisms and the caller only needs `cancel()`. Cancellation
is **racy by contract**: the callback may already be running (or already be
queued for the timer task) when `cancel()` returns, so a caller that must not
act on a stale callback guards the callback body itself.

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
  task context does not return until no callback is running, which is what makes
  releasing the box safe. A handle destroyed from *inside* its own callback is
  the exception (the stop returns immediately there), so the callback copies the
  callable before invoking it and touches the box no further. Consequently
  `cancel()` on this implementation is **stronger** than the abstract contract:
  it is not racy when called from task context. A zero delay or a failed
  `esp_timer_create()` / `esp_timer_start_once()` trips an `assert` rather than
  silently dropping the request (after the assert it returns an empty handle,
  never a handle that owns no timer).
- **`hal_stub`:** `hal/timer_stub.hpp` + `timer_stub.cpp` provide
  `OneShotTimerFactoryStub` and `TimerStub`, likewise pinned by
  `static_assert`.
  It uses FreeRTOS software timers (`freertos/timers.h`) because the `linux`
  target has no `esp_timer` implementation —
  `test-hal-stub/main/test_freertos_timers.cpp` first pinned that the one-shot
  API works there, and `test-hal-stub/main/test_hal_timer_stub.cpp` covers the
  stub itself. Each
  `register_timer()` allocates one `TimerCallbackBox` (the caller's
  `std::function`) and creates a one-shot (`pdFALSE`) timer whose `pvTimerID`
  is that box, so it too is RAII-owned by a move-only handle. Lifetime is
  harder here than on the board, because FreeRTOS timer commands are
  **asynchronous**: start/stop/delete only queue work for the "Tmr Svc" task, so
  neither the timer nor its callback storage can be freed when `cancel()`
  returns. `cancel()` therefore queues the box's free with
  `xTimerPendFunctionCall()`, which FreeRTOS posts to the *same* command queue
  and so processes strictly after the timer's own delete — and the timer task
  drains expired timers before the queue, and the callback copies the callable
  before invoking it, so neither a late callback nor a self-cancel can read
  freed memory. Consequently `cancel()` on the stub is genuinely **racy**, as
  the abstract contract allows: a callback already dispatched may still run
  after `cancel()` returns. A delay that rounds to fewer than one tick (10 ms at
  `configTICK_RATE_HZ` = 100), a null callback, or a failed create/start trips an
  `assert`; after the assert an empty handle is returned.

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
(`Board::TimerHandle = TimerBoard`, `BoardStub::TimerHandle =
TimerStub`) and owns a private timer factory (`OneShotTimerFactoryBoard` /
`OneShotTimerFactoryStub`) that `register_timer()` delegates to. There is **no**
`timer()` accessor on purpose: a caller arms a timer and immediately owns the
handle, so exposing the factory as well would give it two ways to do the same
thing. Because the typedef is spelled by the implementation, `BoardLike` stays
implementation-free — it names only the `TimerLike` concept, so
board-independent code (templated on `hal::BoardLike`) can arm and cancel a
timer without knowing which mechanism is underneath.

The two timer factories are **movable** (defaulted move constructor, move
assignment still deleted, like every other HAL device). They hold no state —
each `register_timer()` creates its own timer — so moving one is
indistinguishable from copying it, and making them movable is what keeps
`Board` / `BoardStub` movable, which `create()` plus
`std::make_shared<Board>(Board::create({}))` relies on. `init_all()` does not
touch the timer: neither `esp_timer` nor the FreeRTOS timer service needs
per-board setup.

The two aggregate boards are **named differently on purpose**:
`hal_board`'s is `thermo::hal::Board` in `hal/board.hpp`, `hal_stub`'s is
`thermo::hal::BoardStub` in `hal/board_stub.hpp`. Nothing includes one header
hoping the other implementation is on the include path, so a project states
which board it linked. `hal_stub`'s devices keep the shared headers
(`hal/led.hpp`, `hal/wifi.hpp`, …), and the shared `WifiRadio` alias still
resolves per implementation. Code that must work with either takes the
`hal::BoardLike` template parameter and never names a concrete board — see
`logic/led_task.hpp`, whose LED-cycle table and coverage checks are free
functions (`thermo::ledtask::led_cycles()` / `has_cycle()`), so the
`static_assert`s naming every `LedState` need no board at all.

**Deep sleep is on the board, not a HAL device.** There is no third sleep
implementation to swap in, and sleeping powers down the *whole* board rather
than a peripheral, so `BoardLike` requires the two board-wide operations
directly: `deep_sleep_start(std::chrono::seconds)` (§3.8) and
`sleep_get_wakeup_causes()`. The vocabulary is in `hal/sleep_types.hpp`
(`hal_common`) — `WakeCause`, `WakeCauses`, `wake_cause_bit()`, `woke_from()` —
and is deliberately dependency-free (only `<cstdint>`): `esp_sleep.h` is not
self-contained on the `linux` target (it includes `hal/gpio_types.h`, which is
not on the include path of a project that links no GPIO driver), and the ESP-IDF
linux port registers no `sleep_modes.c`, so `esp_sleep_get_wakeup_causes()` is
unlinkable there.

`WakeCause` names ESP-IDF's `esp_sleep_source_t` **bit positions**
(`kUndefined` = 0, `kTimer` = 4) so the hardware board passes the ESP-IDF bitmap
straight through; `board.cpp` `static_assert`s that the values still line up.
The wake cause is a bitmap because several sources can fire at once.

`hal_board` calls `esp_sleep_enable_timer_wakeup()` + `esp_deep_sleep_start()`
(the §3.8 pair) and `deep_sleep_start()` is `[[noreturn]]` there. `hal_stub`
records the duration (`last_deep_sleep()`), arms the wake the real board would
produce — `deep_sleep_start()` sets the cause to `WakeCause::kTimer` — and
**returns**. A fresh stub reports a cold boot
(`wake_cause_bit(WakeCause::kUndefined)`), and `set_wakeup_causes()` overrides
either, so a test can simulate any wake. The `BoardLike` concept can therefore
only require that the call is *well-formed* — not `[[noreturn]]` — and no caller
may rely on sleep returning. That asymmetry is deliberate: it is what makes the
sleep/backoff decision host-testable. Run mode now sleeps through the board
(`board.deep_sleep_start(std::chrono::seconds{sleep_s})`), and the RTC boot
policy does the same in reverse: `rtc_state_init(board)` asks
`sleep_get_wakeup_causes()` whether the retained block survives this boot.

**`espidf_cpp` component (`components/espidf_cpp`).** The headers are in
namespace `thermo::espidf_cpp` (`HttpServer` has one `.cpp`, listed in the
component's `SRCS`):

| Header | Provides |
|---|---|
| `espidf_cpp/task.hpp` | `Task<Runner>` (typed `[[noreturn]]` task body), `TaskParams` |
| `espidf_cpp/one_message_queue.hpp` | `OneMessageQueue<T>` (single-slot, trivially-copyable messages) |
| `espidf_cpp/handle.hpp` | `HandleDeleter`, `UniqueTaskHandle`, `UniqueQueueHandle` |
| `espidf_cpp/static_vector.hpp` | fixed-capacity, inline-storage vector |
| `espidf_cpp/static_string.hpp` | fixed-capacity, NUL-terminated string (`try_push_back` / `try_append` for the non-aborting path) |
| `espidf_cpp/http_server_types.hpp` | `HttpServer` vocabulary: `HttpMethod`, `HttpField(s)`, `HttpRequest`, `HttpStatus`, `HttpResponse`, `HttpHandler`, `parse_int`, `normalize_url` (no ESP-IDF dependency) |
| `espidf_cpp/http_server.hpp` | `HttpServer<BodyCapacity>` — prefix routes over `esp_http_server` with one GET/POST handler each; fixed-size request/response buffers, bad input becomes an HTTP error (never an abort) |

Its host tests live in `test-espidf-cpp/` (e.g. `test_one_message_queue.cpp`,
`test_static_vector.cpp`, `test_static_string.cpp`, `test_http_server_types.cpp`,
and `test_http_server.cpp` — which drives a real server over a loopback socket —
plus fork+exec `_assert` programs for the abort paths); `task.hpp` needs a real
scheduler and is verified on target by the firmware build.

**`sml` component (`components/sml`).** A **wrapper**, not a copy: the
state-machine library is the `third_party/sml` git submodule
([boost-ext/sml](https://github.com/boost-ext/sml), pinned to tag **v1.2.0**),
and this component only teaches ESP-IDF about it. Upstream is header-only, so
`SRCS` is empty and `INCLUDE_DIRS` points *outside* the component at
`third_party/sml/include`; consumers name `sml` in `REQUIRES` and include
`<boost/sml.hpp>`. The wrapper exists because the upstream tree carries its own
`CMakeLists.txt` (upstream tests and examples) and must never be discovered as
an ESP-IDF component. It fails loudly at configure time when the submodule has
not been checked out, naming `scripts/dev.py submodules`. The component is also
why `third_party/` stays off every component search path: `donny` names
`third_party/json` explicitly rather than scanning `/third_party`.

**`logic` component (`components/logic`).** The headers are included with the
`logic/` prefix:

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

The RTC state is **split** between the two components, and the split is the
point. `logic/rtc_state.hpp` + `logic/src/rtc_buffer.cpp` own the block's shape
and every operation on it (append/FIFO eviction, clear, cold-boot reset, Wi-Fi
cache clear), so those run in the firmware and the host tests from one
definition. The firmware-only half — `mcgowan/src/rtc_storage.hpp` /
`rtc_storage.cpp` — owns the two things that cannot be host-tested: the single
`RTC_DATA_ATTR` block (which expands to a failing static assert on `linux`)
and the per-boot policy `rtc_state_init(board)`, which clears the block only
when `woke_from(board.sleep_get_wakeup_causes(), WakeCause::kTimer)` is false.
The wake question goes through the board rather than calling
`esp_sleep_get_wakeup_causes()` directly, so the stub board can model any wake;
the policy itself is still firmware-only (it reads `rtc_state()`, which needs
the `RTC_DATA_ATTR` block), while the cold-boot half it delegates to —
`rtc_state_reset()` — is host-tested in `test_rtc_buffer.cpp`.

`LedRunner` and `LedTask` take a `std::shared_ptr<Board>` (never a null one,
where `Board` is the `hal::BoardLike` template parameter) and share the board
with the caller instead of owning it, so the caller can keep using the board
the task drives (e.g. a test reads back the LEDs). Create it with
`std::make_shared<BoardStub>(BoardStub::create(pins))` under `hal_stub`, or the
`thermo::hal::Board` equivalent under `hal_board`, and call `init_all()` before
starting the task. The HAL itself is unchanged: both boards stay non-copyable
and movable.

Layout: `include/logic/` for public headers, `src/` for `.cpp` files
(`src/temp_format.cpp`, `src/rtc_buffer.cpp`, `src/self_signed_cert.cpp`,
`src/rom_id.cpp`). **New `.cpp` files must be added to the `SRCS`
list in `components/logic/CMakeLists.txt`**; adding sources there does not
change any consumer.

**Host-testable modules.** The `logic` component's `temp_format.cpp`,
`rtc_buffer.cpp`, `rom_id.cpp`, and `payload.cpp`, together with
`mcgowan/src`'s `app_config.cpp`, are deliberately free of chip-specific
ESP-IDF dependencies so they build for the `linux` target and are covered by
`test-logic/`. The header-only
`logic/mode_selection.hpp` is the same idea applied to
the mode-sense poles: it depends only on `hal/push_button_types.hpp` (for
`ButtonState`) and `logic/mode.hpp`, and is `constexpr`, so
`test-logic/main/test_mode_selection.cpp` exercises it directly and covers all four
pole combinations — including the both-on hardware fault, which the header
reports as `std::nullopt` instead of rebooting. `main.cpp`'s `read_mode()` is
the thin wrapper that turns that `std::nullopt` into the firmware's fail-loudly
`reboot()`. The HAL's `hal_stub` component (the pin-free buttons, LEDs, 1-Wire
bus, chip-temperature sensor, WiFi radio, one-shot timer, and aggregate board)
is likewise covered by `test-hal-stub/main/test_hal_*.cpp`.
**Keep it that way** — put pure logic in these files and hardware/IDF calls
behind them.

**`donny/`.** A plain (non-test) `linux`-target application that constructs
`thermo::Application<thermo::hal::BoardStub>`. It uses the vendored `json`
component from `third_party/` (its layer requirements are in the
`platform_layers` skill). `hal_board` cannot be linked on the linux target: it
requires `esp_driver_tsens`, whose Kconfig needs
`CONFIG_SOC_TEMP_SENSOR_SUPPORTED`, so
the chain ends at `esp_hal_ana_conv`, a SoC-only component with no linux
implementation (`esp_driver_gpio`, by contrast, registers itself empty on
linux).

**`donny`'s console protocol (`donny/main/console_task.{hpp,cpp}`).** Nothing on
the linux target ever changes the hardware, so a *test driver* has to: the
console task is that channel. It reads one JSON **object** per line from stdin,
applies the fields it names to the `BoardStub`, and writes the resulting state
back as one JSON line on stdout.

- **Task shape.** Split the same way `LedTask` is: `ConsoleRunner` holds the
  behaviour and the `std::shared_ptr<hal::BoardStub>` (shared, never owned), and
  `ConsoleTask : espidf_cpp::Task<ConsoleRunner>` is the FreeRTOS task. It is
  started in `app_main` **before** the `Application` is constructed, so a driver
  can already drive the board while the application boots.
- **Partial documents.** Every input field is optional; a missing field leaves
  that piece of board state alone, so `{"config_button":"on"}` presses one pole
  without touching anything else. Output is always a complete snapshot.
- **Idempotent round trip.** Feeding the task's own output back in changes
  nothing and yields byte-identical output, so a driver can read the state, edit
  one field, and write it back without knowing which fields exist. Three
  reported fields are therefore **ignored on input** because nothing in the
  board's interface can set them: `wifi.initialized` (only `init()` sets it),
  `deep_sleep.last_s` (the only way to sleep is to end the process), and — see
  below — `wifi.ap` / `wifi.station` when the interface is down.
- **Fields.** `button`, `config_button`, `run_button` (`"on"`/`"off"`);
  `leds.{green,red}` (`"on"`/`"off"`, a JSON bool is accepted on input);
  `chip_temp_c_x100` (int16); `sensors` (an array of `{rom, temp_c_x100}`, a
  complete replacement of the bus, `null` temperature for a failed reading);
  `wifi.{initialized, ap_running, station_running, ap, station}`;
  `deep_sleep.last_s` (`null` until the board has slept).
- **WiFi config is start-only.** The HAL can record a configuration *solely* by
  starting the interface (`start_ap()` / `start_station()`), so naming a `wifi.ap`
  / `wifi.station` object starts it, and `*_running` alone starts/stops with the
  configuration already recorded. Per interface the configuration is resolved
  *before* the running state, so the result never depends on JSON key order. A
  stopped interface reports `null` for its configuration (that is the only state
  a driver can round-trip), and restarting it reuses the recorded one.
  Contradictory input (`ap` with `ap_running:false`) is an error.
- **Errors never kill the task.** A malformed line answers
  `{"error":"malformed JSON"}`; a rejected field answers
  `{"error":"...","state":{...}}` with the state after the fields that *did*
  apply, and the loop continues. EOF (the driver closed the pipe) makes the task
  idle on `vTaskDelay(60 s)` rather than spin.
- **stdout is the protocol; logs go to stderr.** ESP-IDF logs to stdout by
  default (`esp_log_vprint_func = &vprintf`) and the stub devices log when they
  are written to, which would interleave log lines with protocol output.
  `app_main` installs `esp_log_set_vprintf()` via a **static-init** struct: doing
  it inside `app_main` is too late, since the port logs "Starting scheduler"
  first.
- **stdin is read with `read()`, never `fgets`.** The ESP-IDF `linux` FreeRTOS
  port drives the scheduler from a 1 kHz `SIGALRM` whose handler it installs with
  `sa_flags = 0` (no `SA_RESTART`), so *every* blocking read returns early with
  `EINTR`. `fgets` cannot distinguish that from end of file: it sets the sticky
  error flag and returns `NULL`, so an interactive console saw "EOF" about a
  millisecond after it started and shut its input down (a pipe only appeared to
  work because the data was already buffered). `read()` keeps `errno`, so the
  console task simply retries on `EINTR`; reading the descriptor directly also
  sidesteps the `FILE` error flag.
- **The JSON library is vendored in `third_party/`.** `third_party/json` holds
  `nlohmann/json` (v3.11.3, single header, MIT + `LICENSE.MIT`) as a
  header-only ESP-IDF component. It sits in `third_party/` rather than under
  `components/` or `donny/` because `third_party/` is outside every directory
  `scripts/dev.py` clang-formats: a vendored header must stay byte-identical to
  upstream so it can be diffed against a release. No project sees it by
  default — `donny/CMakeLists.txt` opts in with
  `list(APPEND EXTRA_COMPONENT_DIRS .../third_party/json)` (the *component
  directory*, not the whole of `third_party/`, so the `sml` submodule's own
  upstream `CMakeLists.txt` can never be discovered) and `main` names `json` in
  its `REQUIRES`. The firmware deliberately does not use it: it formats its
  payload by hand and has no JSON parser, whereas `donny`'s binary size is
  irrelevant. `donny` builds with `CONFIG_COMPILER_CXX_EXCEPTIONS=n`, so the
  parse uses the non-throwing form (`json::parse(line, nullptr, false)` +
  `is_discarded()`), never `try`/`catch`.
- **The state-machine library is a git submodule.** `third_party/sml` is
  [boost-ext/sml](https://github.com/boost-ext/sml) at tag **v1.2.0**
  (commit `2d8485d`), recorded in `.gitmodules` and used by the `logic`
  component for the application state machines. `components/sml` is a thin
  ESP-IDF wrapper that exposes the submodule's `include/` directory; `logic`
  names `sml` in `REQUIRES`, so consumers include `<boost/sml.hpp>` and never
  name the component themselves. The upstream tree is **never** a component:
  its own `CMakeLists.txt` builds upstream tests/examples, so it stays off every
  component search path (`donny/CMakeLists.txt` names
  `third_party/json` explicitly instead of scanning `third_party/`), and it is
  outside `ROOTS`, so clang-format never touches it (`_is_vendored` keeps
  clang-tidy off it too).
  - **Not vendored, not committed content.** The submodule is pinned by the
    gitlink recorded in git, so a fresh clone has an empty directory until the
    sources are fetched. `scripts/dev.py submodules` fetches them
    (`git submodule update --init --recursive -- <path>`, idempotent), and
    `build`, `test`, and `clang-tidy` call the same helper first, so a bare
    clone builds without a manual step. CI runs `submodules` as its own step,
    before `build`, so a failed fetch is reported on its own rather than as a
    confusing configure error.
  - **Updating the pin:** `git -C third_party/sml checkout v1.2.1`, then
    `git add third_party/sml` in this repo (and update the version in this
    document and `components/sml/CMakeLists.txt`). `scripts/dev.py self-check`
    pins the gitlink to the v1.2.0 commit.

**Not yet present (expected next):**
- On-target tests for hardware-dependent code (1-Wire, deep-sleep, Wi-Fi).

**Entry point:** In ESP-IDF, `app_main(void)` is the firmware entry point and must be declared with C linkage:
```cpp
extern "C" void app_main(void);
```

## Development Workflow

> **Important:** All builds run inside the ESP-IDF Docker environment. The environment is defined by the committed `Dockerfile` + `docker-compose.yml`, but the image itself is **prebuilt and pulled** (`marcpawl/esp-idf:20261109b`); use `docker compose run --rm dev <cmd>` (or `./scripts/dev.py shell <cmd>`) rather than ad-hoc `docker run`. `sdkconfig` is generated (git-ignored) — edit `sdkconfig.defaults` instead when changing configuration intent.

### Development environment setup

All builds run inside the prebuilt ESP-IDF Docker image to guarantee a reproducible toolchain (ESP-IDF v6.1, Xtensa GCC 15.2.0, CMake, Ninja, Python venv). The image `marcpawl/esp-idf:20261109b` is pinned in `docker-compose.yml`, is never moved, and is the same image CI pulls, so there is nothing to build locally:

```bash
# From the repository root, pull the pinned dev image (first time, or to refresh)
./scripts/dev.py pull       # == docker pull marcpawl/esp-idf:20261109b

# Interactive shell with the ESP-IDF environment auto-sourced
docker compose run --rm dev
```

Only rebuild when the `Dockerfile` itself changes: `docker compose build` (rebuilds and tags it as the pinned image), then publish a **new** date-stamped tag and bump the pin in `docker-compose.yml` + `scripts/dev.py` (`DEV_IMAGE`). See `Developer.md` ("Updating / publishing the dev image").

> **Preferred way to run a command in the container from the host.**
> Use `./scripts/dev.py shell <command> [args...]` instead of ad-hoc
> `docker compose run`, which guarantees the container user mapping and the
> ESP-IDF environment are set up exactly as the documented tasks expect:
>
> ```bash
> # Interactive login shell (no arguments)
> ./scripts/dev.py shell
>
> # Run a one-off command in the container shell (arguments passed through)
> ./scripts/dev.py shell ls -1d /
> ./scripts/dev.py shell "echo hi | wc -l"   # shell syntax works too
> ```

The container runs as **your own** UID/GID (`scripts/dev.py` passes `--user "$(id -u):$(id -g)"`), so build artifacts are owned by you and no chown or user-namespace remapping is involved; the `dialout` group is joined for serial access. `$HOME` is a directory that uid can actually write (see the next note), so ccache works.

> **ccache and non-1000 uids.** A bind mount does not translate uids, so the container runs as the calling user rather than remapping it. The image's own user is uid 1000; any other uid (a normal host user, or GitHub's runner at 1001) has no passwd entry inside the image, so `$HOME` resolves to `/` and ccache fails with `ccache: error: Permission denied`. `scripts/dev.py` therefore exports `HOME=/tmp` and `CCACHE_DIR=/tmp/.cache/ccache` for any uid other than 1000 (uid 1000 keeps `/home/dev`). Keep this in mind if you add another `$HOME`-caching tool.

Inside the container, ESP-IDF is already on the environment; if needed, source it manually:

```bash
. $IDF_PATH/export.sh
```

### CLion: work inside the container

CLion 2026.2 is wired to the **same** compose `dev` service through a Dev
Container, so the IDE pulls the pinned image and starts the container
automatically ("starts if necessary") and the toolchain, CMake, Ninja, and the
ESP-IDF Python venv all run **inside** it. No host-side ESP-IDF install is
required.

- **Definition:** `.devcontainer/devcontainer.json` (reuses `docker-compose.yml`;
  it never defines its own image).
- **Connect:** open the repo in CLion and accept the Dev Container prompt, or use
  *Remote Development → Dev Containers → docker-compose.yml → `dev`*. CLion
  pulls `marcpawl/esp-idf:20261109b` if it is not cached and starts the service on
  demand. Note that compose falls back to building the local `Dockerfile` if the
  pull fails, so run `./scripts/dev.py pull` first if you want that failure to be
  loud.
- **Toolchain/CMake:** select the container as the remote host (SSH into the Dev
  Container). CLion configures and builds **each subproject** under its own
  CMake build directory (all from `.idea/cmake.xml`):
  `mcgowan/cmake-build-esp32s3/`, `test/cmake-build-linux-test/`,
  `espidf-cpp-test/cmake-build-linux-espidf-cpp-test/`,
  `donny/cmake-build-linux-donny/` — so the IDE and `idf.py` never share a CMake
  cache. `mcgowan/build_esp32s3/` remains the canonical `idf.py` build used by
  `scripts/dev.py build`/`flash`.
- **Why one CMake profile per subproject.** A CLion CMake profile has no way to
  point CMake at a *sub*directory — a profile *is* a project. The repo root is
  only an aggregator (`project(... NONE)`) that shells out to `idf.py`, so a
  root profile enables no C/C++ languages, emits no `compile_commands.json`, and
  yields no code model (no navigation, no refactorings). Every subproject is a
  standalone ESP-IDF project and ESP-IDF sets `CMAKE_EXPORT_COMPILE_COMMANDS` on
  by default (`$IDF_PATH/tools/cmake/project.cmake`), so each one produces a
  database that CLion turns into a full resolve configuration. The four
  profiles coexist rather than being merged because the projects share sources
  (`mcgowan/src` is compiled by both `mcgowan` and `test`; `components/*` by
  three or four of them) and genuinely need different defines/include paths —
  notably `hal_board` (Xtensa) vs `hal_stub` (host/linux). The active profile is
  chosen by the run configuration's `CONFIG_NAME`.

  > **Verified reality (2026-10-27): the profiles do not deliver that yet.**
  > `GENERATION_DIR` sets only the *build* directory; the *source* directory is
  > the single CMake workspace in `.idea/misc.xml`
  > (`CMakeWorkspace PROJECT_DIR="$PROJECT_DIR$"`), which is the repo root for
  > every profile. After a clean CLion rebuild all four `cmake-build-*` trees
  > recorded `CMAKE_HOME_DIRECTORY=/project`, none contained a
  > `compile_commands.json`, and each codemodel named project
  > `esp32_sensors_subprojects` with only the four aggregate custom targets —
  > all `UTILITY`, i.e. no compilable translation units. So all four profiles
  > build the same root aggregator, `-DIDF_TARGET=...` is unused by
  > `project(... NONE)`, and CLion gets no code model from them. Fixing it
  > needs either one CLion project per subproject or a merged
  > `compile_commands.json` exposed at the root. Until then `scripts/dev.py
  > build` is the way to build, and navigation/refactorings are not driven by
  > these profiles.

> Editing the image, mounts, or user? Change `docker-compose.yml` (and `Dockerfile`),
> **not** `devcontainer.json`. The Dev Container deliberately inherits everything
> from the compose service so the CLI and the IDE cannot drift apart.

#### Run configurations

Eight shared run configurations are committed under `.idea/runConfigurations/`
(and mirrored in the local `.idea/workspace.xml`). Six call `scripts/dev.py`,
so a single script is the source of truth for container commands — run them
from the CLion Run/Debug toolbar. The other two kinds are native CLion **CMake**
run configurations: the four below (one per subproject profile) are meant to
drive CLion's own CMake project model, which is what would make the compile
database live and therefore enable code navigation and C++ **refactorings** —
but see the "Verified reality" note above: they currently all configure the
root aggregator and produce no compile database, so today they only build.
`PROJECT_NAME` in each must match the root `project()` name,
`esp32_sensors_subprojects`, and `TARGET_NAME` must name a target **of that
root project** — i.e. one of the `*_build` aggregate targets, not a subproject's
ESP-IDF `<name>.elf`. The root is `project(... NONE)` and defines only the
aggregate custom targets, so an ELF name is unresolvable there and CLion
reports "cannot find target". CLion rewrites `PROJECT_NAME` to the real root
name once it has resolved the project, so a stale value there is a symptom,
not a fix; `ClionRunConfigurationTests` pins both invariants.

| Run configuration | Kind | What it does |
|---|---|---|
| esp32_sensors (container CMake) | CMake | Builds the `mcgowan_build` aggregate target via the `Espressif ESP32-S3` profile; intended (not yet achieved) to index the firmware for refactorings |
| Host tests (test, container CMake) | CMake | Builds the `test_build` aggregate target via the `Host tests (test)` profile |
| Host tests (espidf-cpp-test, container CMake) | CMake | Builds the `test-espidf-cpp_build` aggregate target via the `Host tests (espidf-cpp-test)` profile |
| Linux app (donny, container CMake) | CMake | Builds the `donny_build` aggregate target via the `Linux app (donny)` profile |
| Build firmware (container) | `scripts/dev.py build` | `idf.py -C mcgowan -B mcgowan/build_esp32s3 build` (and the other subprojects) |
| Flash & monitor (container) | `scripts/dev.py flash` | `idf.py -C mcgowan -B mcgowan/build_esp32s3 -p $ESP32_PORT flash monitor` (default `/dev/ttyUSB0`) |
| Host unit tests (container) | `scripts/dev.py test` | runs all four host test suites (below), `test-hal-common` first and `test-hal-stub` next; `test-hal-common` / `test-hal-stub` / `test-logic` / `test-espidf-cpp` run one each |
| clang-format check (container) | `scripts/dev.py format-check` | `clang-format --dry-run --Werror` |
| menuconfig (container) | `scripts/dev.py menuconfig` | Kconfig UI |
| Container shell (ESP-IDF) | `scripts/dev.py shell` | login shell with the IDF env sourced (accepts trailing args to run a command) |

Set `ESP32_PORT` (e.g. `/dev/ttyACM0`) to target a different serial device.

> **Building is container-only.** The root `CMakeLists.txt` and every subproject
> refuse to configure unless `DOCKER_IMAGE=esp32_sensors` (set by the `Dockerfile`), so a
> host invocation fails fast. The CMake profiles in `.idea/cmake.xml` and the
> CMake run configurations all use the Dev Container toolchain, so CLion builds
> and refactors through the container.

#### Shared vs. local `.idea` files

The files needed to open, build, and refactor the project are **tracked** so a
change to the shared configuration shows up in the change list:

- Committed (shared): `.idea/cmake.xml` (CMake profiles), `.idea/runConfigurations/*`
  (run/debug configurations), `.idea/codeStyles/*` (formatting used by
  refactors), `.idea/misc.xml`, `.idea/modules.xml`, `.idea/esp32_sensors.iml`,
  `.idea/vcs.xml`, `.idea/editor.xml`.
- Local only (git-ignored): `.idea/workspace.xml` and `.idea/shelf/` — these
  hold per-machine IDE state, not shared configuration.

The root `.gitignore` ignores `.idea/*` and then un-ignores the shared files
above; see the "IDE / editor" section there.



### python scripts/dev.py

dev.py can be executed on the host or in the container.
Command Format is 
```bash
./scripts/dev.py COMMAND
'''

Platform is either host or both.  Host only commands will fail
when enacted in a container.  Commands that are both will
create and execute the same command in a container when
enacted on the host.

|platform|command|action|
|===|===|===|
|host|pull|Pull the pinned prebuilt dev image from the registry|
|host|image|Build the dev image locally from the Dockerfile (not needed normally)|
|both|submodules|Fetch the pinned git submodules (`third_party/sml`)|
|both|build [SUBPROJECT...]|Build all subprojects via the root aggregator, or just the named ones (mcgowan, test-hal-stub, test-hal-common, test, test-espidf-cpp, donny)|
|both|build-test-espidf-cpp|Build the espidf_cpp unit tests for the ESP-IDF `linux` target|
|both|test|Run every host unit test suite: `test-hal-common`, then `test-hal-stub`, then `test-logic`, then `test-espidf-cpp`|
|both|test-hal-stub|Build and run the `test-hal-stub/` host unit tests (ESP-IDF `linux` target)|
|both|test-hal-common|Build and run the `test-hal-common/` host unit tests (ESP-IDF `linux` target)|
|both|test-logic|Build and run the `test-logic/` host unit tests (ESP-IDF `linux` target)|
|both|test-espidf-cpp|Build and run the `test-espidf-cpp/` host unit tests (ESP-IDF `linux` target)|
|both|self-check|Run the host unit tests for dev.py itself (stdlib `unittest`)|
|both|flash|idf.py flash monitor|
|both|monitor|idf.py monitor|
|both|menuteconfig|idf.py menuconfig|
|both|format|clang-format all source code (tracked **and** untracked, so a new file needs no `git add` first)|
|both|format-check|Check if code is formatted, non-zero exit if anything is unformatted |
|both|format-check-staged|Check if staged code is formatted, non-zero exit if anything is unformatted |
|both|pycodestyle|Python style check (PEP 8, 79 columns) over every Python source in the repository — `*.py` plus extensionless Python shebang scripts such as `.githooks/pre-commit`; `third_party/` is excluded; non-zero exit on any finding|
|both|clang-tidy [PROJECT...]|Static analysis over the built projects' C++ sources (needs a prior `build`); non-zero exit on any finding|
|both|shell|Run a bash command in the container (arguments after `shell` are executed); with no arguments, an interactive login shell|
|both|cn|Run the Continue CLI (`cn`) in the container with the host's `~/.continue/config.yaml` passed as `--config`; arguments after `cn` go to `cn` verbatim (e.g. `cn -p "prompt"`)|
|both|complete|Print subcommands matching a prefix (internal; used by bash completion)|
|host|install-hooks|install git commit hooks|

### Shell completion

`scripts/dev.py-completion.bash` provides bash tab-completion for the
subcommands. Enable it for the current shell or permanently via `~/.bashrc`:

```bash
source scripts/dev.py-completion.bash
```

Then `./scripts/dev.py form<TAB>` completes to `format`, `format-check`,
`format-check-staged`, and `./scripts/dev.py clang-<TAB>` to `clang-tidy`. The
script delegates candidate lookup to
`dev.py complete <prefix>`, so the command list has a single source of truth.

### Container commands
```bash
# Build every subproject via the root aggregator (plain CMake: configure, then build).
docker compose run --rm dev cmake -S . -B build_root && \
    docker compose run --rm dev cmake --build build_root

# Build just the firmware: idf.py runs in mcgowan/ with its build tree inside it.
docker compose run --rm dev idf.py -C mcgowan -B mcgowan/build_esp32s3 build
```

C++26 (`-std=gnu++26`) is the ESP-IDF v6.1 default for chip targets — no extra compiler flags are required.

### Flash and monitor

The host `/dev` tree is bind-mounted into the container.  The board
can be plugged in at any time and it will be accessible by
the container.

```bash
./scripts/dev.py flash
```

### Testing approach

There are two kinds of test suite, runnable from the host via `scripts/dev.py`:

- **Host-based unit tests** build for the ESP-IDF `linux` target with Unity — no
  hardware needed — in four projects:

  - `test-hal-stub/` covers the pin-free `hal_stub` devices. Each device's own
    file exercises its stub contract and its `hal_common` concept:
    `test_hal_led.cpp` (`LedLike`), `test_hal_push_button.cpp`
    (`PushButtonLike`, including the `set()` control the mode-sense poles use),
    `test_hal_onewire.cpp` (`OneWireLike` plus `set_sensors()`),
    `test_hal_chip_temperature.cpp` (`ChipTemperatureLike`), `test_hal_wifi.cpp`
    (`WifiRadioLike`, AP/station start/stop independence, and the empty-SSID
    rejection), and `test_hal_timer_stub.cpp` (`TimerStub` /
    `OneShotTimerFactoryStub`: firing exactly once, a captured callable really
    running, cancelling before and after the expiry, and move construction /
    move assignment transferring ownership). `test_freertos_timers.cpp` verifies
    that FreeRTOS software timers — what the stub timer is built on, since the
    `linux` target has no `esp_timer` — create, fire exactly once, re-arm, and
    stay silent after a cancel. `test_hal_board.cpp` covers the aggregate
    `BoardStub`: that `config_button()` / `run_button()` / `wifi()` are wired
    up, that `init_all()` initializes but does not start the radio, that
    `deep_sleep_start()` records the duration while
    `sleep_get_wakeup_causes()` / `set_wakeup_causes()` model the next boot's
    wake cause, and that the board names a cancelable `TimerHandle` and arms
    working timers through `register_timer()`.
  - `test-hal-common/` covers the implementation-free `hal_common` vocabulary.
    `test_hal_board_types.cpp` pins the `BoardLike` contract from the other
    side: `BoardPins` defaults to the §2.4 pin map, `BoardWithoutTimer` spells
    out every requirement except the timer using the real stub devices, and
    `ModelBoard<Handle, Returned>` adds just the timer clause, so a model that
    names no handle, a non-class or non-cancelable handle, a `register_timer()`
    returning something other than the board's `TimerHandle`, or no
    `register_timer()` at all is each rejected for that reason alone.
    `test_hal_timer_types.cpp` models conforming and non-conforming
    timer/handle types to prove both concepts reject what they should, and
    `test_hal_sleep_types.cpp` pins the wake-cause vocabulary (`WakeCause` at
    ESP-IDF's bit positions, `wake_cause_bit()`, and `woke_from()`).
  - `test-logic/` covers the pure-logic modules (`temp_format`, `limits`,
    `led_manager`, `payload`, `app_config` validation, `rom_id`, `rtc_buffer` —
    including the cold-boot `rtc_state_reset()`, and the `mode_selection` pole
    mapping) and the firmware sources in `mcgowan/src` that need no chip driver
    (`app_config.cpp`, `config_page.cpp`, `power_source.cpp`).
    `test_led_runner.cpp` instantiates the templated `LedRunner<Board>` against
    the stub `BoardStub`.
  - `test-espidf-cpp/` covers the `espidf_cpp` wrappers (the headers listed
    under "`espidf_cpp` component" above).
- **On-target tests** are planned for hardware-dependent code (1-Wire/DS18B20
  reads, deep-sleep wake, Wi-Fi upload).
- Keep hardware interactions behind thin interfaces so business logic remains
  host-testable. When adding pure logic that the firmware and the tests both need,
  put it in `components/logic` (headers under `include/logic/`, `.cpp` files in
  its `SRCS`); when it is firmware-only, add it to both
  `mcgowan/main/CMakeLists.txt` and `test-logic/main/CMakeLists.txt`.

Run the suites with `./scripts/dev.py test` (all four, stopping at the first
failure), or run one with `test-hal-common`, `test-hal-stub`, `test-logic`, or
`test-espidf-cpp`. See the `quality_checks` skill for the gates and the fixing
policy.

### Quality checks

Run every quality gate through `scripts/dev.py` and fix what it reports. The
gates — clang-format, pycodestyle, clang-tidy, the build, the four host test
suites, and the `dev.py` self-check — their order, and the fixing policy are
documented in the `quality_checks` skill
(`.continue/skills/quality_checks/SKILL.md`). `.githooks/pre-commit` enforces
them before every commit.

> **Parked defect:** `pins::kDividerGate` is 25, but the ESP32-S3 has no GPIO
> 22–25 at all (`soc/gpio_num.h` jumps `GPIO_NUM_21` straight to
> `GPIO_NUM_26`), so `battery_monitor.cpp`'s cast to `gpio_num_t` addresses a
> pin that does not exist. `Specification.md` §2.4 still says "GPIO 25
> (optional)", which is classic-ESP32 numbering, so the spec and this SoC
> disagree. The pin map is deliberately left alone until that is settled; the
> analyzer finding is suppressed there with a comment pointing at this note.

### C++ coding standards

How the C++ itself is written — naming, RAII and ownership, smart pointers,
`const`/`constexpr` and immutability, Rule of Zero/Five, `enum class` over
plain `enum`, templates and concepts, concurrency, standard library use, and
the `.hpp` header convention — is in the `cpp-coding-standards` skill
(`.continue/skills/cpp-coding-standards/SKILL.md`), derived from the C++ Core
Guidelines and ending with a review checklist. Load it when writing,
reviewing, or refactoring C++.

The skill already records this repository's two deviations from upstream —
`.hpp` headers instead of `.h`, and `#pragma once` instead of `#ifndef`
include guards — so follow the skill's wording and never reintroduce the
upstream form.

The skill's `try`/`catch` guidance does not apply here either. The firmware
builds with `CONFIG_COMPILER_CXX_EXCEPTIONS=n` and
`CONFIG_COMPILER_CXX_RTTI=n` (`mcgowan/sdkconfig.defaults`), so there is
nothing to throw or catch and no `dynamic_cast`; failures take the
`assert()`/`abort()` path described under "Fail loudly" below.

### CMake

How the `CMakeLists.txt` files are written — including the rule that they
document the build and never the sources — is in the `cmake` skill
(`.continue/skills/cmake/SKILL.md`). Read it before editing a build file or
adding a source.

### Conventions to follow

The bullets below are this repository's additions to, and overrides of, the
`cpp-coding-standards` skill; anything the skill already states is not
repeated here.

- Target **ESP-IDF v6.1** APIs only.
- Use **C++26** with GNU extensions (`-std=gnu++26`, the ESP-IDF v6.1 default
  for chip targets).
- Declare `app_main` with `extern "C"` linkage.
- Treat RTC-memory structures carefully: they must be **trivially copyable / `POD`-like** with stable layout and `RTC_DATA_ATTR` placement, and must survive both deep sleep and reboots.
- Conform payload format, LED semantics, and RTC memory layout exactly to `Specification.md`.
- Keep power draw within the hourly budget defined in the spec (batch uploads, deep sleep between samples).
- **Fail loudly, not silently.** When a precondition or capacity limit is violated, `assert()` (or `abort()`) instead of silently truncating, clamping, or dropping data. Silent fallbacks hide bugs; `assert()` in this firmware is fatal and makes the contract explicit. Only use a bounded/tolerant path (e.g. a `try_*` API or an explicit early `return`) when the recovery is deliberate and the caller is expected to handle it.
- ** asserts can be tested.** For classes that are being tested on linux that have an assert test the the assert is triggered. 
Assert tests should be in a stand alone program whose name ends with "_assert".  The assert program should take as a command line parameter the name of the test case to execute, if there is no name, or if the name is invalid the program should exit with 0. 

The test case should fork and exec the "_assert" program with the name of the test case, wait for the child, and confirm that it was aborted.

- **C++ header blocks are ordered and warning-scoped.** Every C++ source
  (`.cpp`) and header (`.hpp`) file's include block follows this order,
  separated into the groups below:

  1. **The file's own header**, outside the wrapper (a `.cpp`'s own header, or
     a `.hpp` that includes itself by name) — parsed before the suppression
     scope so its diagnostics are never masked. Test sources, whose first
     include is `unity.h`, have no own header and start at step 2.
  2. **Turn off clang-tidy** for the block (`// NOLINTBEGIN`).
  3. **`#pragma GCC diagnostic push`**, then (only if needed) disable the
     warnings that come from FreeRTOS/ESP-IDF headers, so third-party warnings
     never leak into our build. The list is deliberately empty today: the
     third-party headers in use are warning-clean at the build's warning level,
     and inventing flags for warnings that do not fire only adds noise. Add an
     ignore here when a new ESP-IDF header actually trips a diagnostic.
  4. **`#include` the FreeRTOS headers.**
  5. **`#include` the ESP-IDF headers.**
  6. **`#pragma GCC diagnostic pop`** — restore warnings.
  7. **Turn clang-tidy back on** (`// NOLINTEND`).
  8. **`#include`s for C++ libraries.**
  9. **`#include`s for C libraries.**
  10. **`#include`s for system headers.**
  11. **All other project `#include`s.**

  Keep each group separated by a blank line. `clang-format` (`SortIncludes:
  CaseSensitive`, `IncludeBlocks: Preserve`) sorts each group but does not
  reorder across the blank lines, so the groups must be laid out by hand.
  Write includes fully qualified (`"esp_log.h"`, `"freertos/FreeRTOS.h"`,
  `"hal/push_button.hpp"`) so the group is unambiguous:

  ```cpp
  #include "sensor.hpp"

  // NOLINTBEGIN
  #pragma GCC diagnostic push  // NOLINT(ignored diagnostics)
  #include "esp_log.h"
  #include "freertos/FreeRTOS.h"
  #include "freertos/task.h"
  #pragma GCC diagnostic pop

  // NOLINTEND

  #include <cstdint>
  #include <cstring>
  #include <sys/types.h>

  #include "hal/push_button.hpp"
  ```

  clang-format inserts the blank line after `#pragma GCC diagnostic pop`
  unless another pragma follows it; run `scripts/dev.py format` after editing
  an include block.

### Agent interaction

#### Committing

**Write the commit message to a temporary file, then commit with `git commit
-F <file>`.** Never pass it inline with `-m`, and never run a bare
`git commit`. Show the file to the user and wait for their approval before
committing: they may want to edit the message, and editing it afterwards
needs a rebase.

The message shape (subject ≤ 72 columns, imperative, no trailing period; a
blank line; a body wrapped at 72–79 columns that explains **why**; the
Continue trailer), why a file beats `-m`, and a worked example are in the
`git` skill at `.continue/skills/git/SKILL.md` — read it before writing a
message.

#### Model selection

Prefer the less expensive **DeepSeek** model for trivial work, and escalate
only when something fails. The default routing is:

| Work | Model |
|---|---|
| Running `./scripts/dev.py` commands | DeepSeek Chat |
| Running `git` commands | DeepSeek Chat |
| Fixing `.hpp` / `.cpp` clang-format failures | DeepSeek Chat |
| Performing the pre-commit checks | DeepSeek Chat |
| Making file changes once the work is figured out | DeepSeek Chat |
| Fixing a failing `./scripts/dev.py` command | DeepSeek Reasoner |
| Fixing a failing `git` command | DeepSeek Reasoner |

- **Escalate on failure, do not retry.** A failing step is handed to DeepSeek
  Reasoner rather than retried under DeepSeek Chat. This is the default;
  follow any more specific instruction given for the task at hand.
- **Fixing a failing pre-commit check** follows the rule for whatever the
  check reported — a clang-format failure stays on DeepSeek Chat, while a
  clang-tidy or test failure escalates to DeepSeek Reasoner.
- **Reason first, edit second.** Work out *what* to change with the stronger
  model, then hand the mechanical edits to DeepSeek Chat.

- **Ignore the CLion configuration.** Unless explicitly told otherwise, do not
  read, edit, stage, commit, or otherwise consider the CLion configuration files
  (`.idea/*`, including `.idea/cmake.xml`, `.idea/runConfigurations/*`,
  `.idea/misc.xml`, `.idea/workspace.xml`, etc.). CLion rewrites these on its own
  and any changes it makes are noise unrelated to the task at hand. If such a
  file shows up modified or deleted in `git status`, leave it alone — do not
  "fix" or restore it — unless the user specifically asks about CLion config.
- **Moving a class: ask about its dependencies first.** Before moving a class
  to another component/project, identify what that class depends on (and what
  depends on it). If any dependency would also have to move, **do not start
  the move** — ask the user first whether that dependency should move too.
  There is a good chance the user will abort the combined move and move the
  dependencies one at a time, in their own steps, first. Wait for that
  decision before editing anything.
- **Found a bug in a class you are using but that is unrelated to the current
  task? Do not fix it on the current branch.** Instead, park the work and fix
  it on its own branch so the fix can be reviewed and merged independently:
  1. **Stash** the in-progress changes with a message that describes the work
     done so far and what is still pending for the task, e.g.
     `git stash push -m "WIP <task>: done ...; pending ..."`.
  2. **Branch** from the base of the current branch (the commit the current
     branch was started from, not the current branch tip):
     `git switch -c fix/<short-name> <base-commit>`.
  3. **Fix** the problem, add tests where the bug is host-testable, then
     **pull and rebase** the current branch (`git pull --rebase`), format-check
     the staged files, commit, and **push** the branch.
  4. **Wait** to be told the fix has been merged. Do not continue as if the
     fix were available on the original branch.
  5. Once merged, **delete** the fix branch and re-sync the original branch
     (`git pull --rebase`) so it includes the merged fix.
  6. **Return** to the original branch and `git stash pop`; read the stash
     message to recall what was done and what was pending, then **resume** the
     original task.
- When the current work item is complete and no further input is expected,
  output `DONE!` as the final line of your response in the Continue (`cn`)
  CLI. Do **not** print `DONE!` if there are follow-up questions, pending
  sub-tasks, or work awaiting user confirmation.
