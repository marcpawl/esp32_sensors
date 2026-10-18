# Repository Overview

## Project Description

This repository contains an **ESP32 Solar-Powered Thermometer Logger** and its software specification (Rev 12). Firmware implementation is **in progress**: Configure mode is fully implemented; Run mode is scaffolded with the network paths (Wi-Fi station, batch upload, backoff) intentionally stubbed.

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
| Container image | `espressif/idf:release-v6.1` |
| Source language | **C++26** (GNU extensions, `-std=gnu++26`) |
| Target framework | ESP-IDF v6.1 |
| Toolchain | GCC 15.2.0 (Xtensa `esp-15.2.0_20251204`, bundled with ESP-IDF v6.1) |
| License | GPL-3.0 |

> **Note:** Firmware source lives in the `mcgowan/` subproject and builds successfully (`mcgowan.bin` has been produced). `Specification.md` remains the authoritative source of truth for all implementation work; where code and spec disagree, the spec wins. The `install.sh` at the repo root is an unrelated Continue CLI installer artifact and is **not** part of the firmware.

> **Implementation status (current milestone):**
> - **Configure mode — implemented.** SoftAP, HTTPS server, config UI, NVS persistence, LED semantics.
> - **Run mode — scaffolded/stubbed.** Real: boot self-test LEDs, battery read + hysteresis, sensor read with cached ROM IDs (first-boot scan), sample append to the RTC buffer, deep sleep. **Stubbed:** Wi-Fi station connect + cached fast-reconnect (§3.9), batch upload + payload wire-out (§9, payload is formatted and logged but not sent), exponential backoff (§3.7).
> - **Host unit tests + QEMU smoke test.** `test/` builds for the ESP-IDF `linux` target (Unity) and `qemu/` boots an ESP32-S3 image under QEMU; both run via `scripts/dev.py test` (or `test-linux` / `test-qemu`). On-target tests for hardware-dependent code are planned.

> **Toolchain note:** The host's locally installed ESP-IDF is **v6.1** with **Xtensa GCC 15.2.0** (`esp-15.2.0_20251204`), which matches the pinned Docker image `espressif/idf:release-v6.1` exactly. Keep both in sync: if you upgrade the host toolchain, update the pin in `Dockerfile`/`docker-compose.yml` and this document.

## Architecture Overview

The intended system architecture (per `Specification.md`) is a single ESP-IDF firmware application running on an ESP32 with the following subsystems:

- **Sensor layer** — 1-Wire bus driver reading three DS18B20 temperature sensors.
- **Mode controller** — reads the 3-position switch to select Configure / Off / Run.
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
├── Specification.md     # Authoritative software spec (Rev 12) — read this first
├── RELEASE_NOTES.md     # Human-readable release notes
├── CMakeLists.txt       # Root aggregator (plain CMake) — builds all subprojects
├── mcgowan/             # Firmware subproject (ESP32-S3, hal_board) -> mcgowan.bin
│   ├── CMakeLists.txt   #   standalone ESP-IDF project (project(mcgowan))
│   ├── sdkconfig.defaults
│   ├── main/            #   ESP-IDF `main` component (must be named main)
│   │   └── CMakeLists.txt
│   └── src/             #   C++26 sources + include/ — see module map below
├── components/          # Reusable ESP-IDF components (see "HAL" section)
│   ├── espidf_cpp/      # C++ wrappers over ESP-IDF/FreeRTOS (Task, queue, handles, ...)
│   ├── hal_common/      # Shared, header-only HAL vocabulary (types/concepts)
│   ├── hal_board/       # Real, GPIO-backed HAL (linked by mcgowan)
│   └── hal_stub/        # Pin-free HAL stub (linked by QEMU/host tests)
├── test/                # Host-based unit tests subproject (IDF `linux`, Unity)
├── espidf-cpp-test/     # Host tests for components/espidf_cpp (IDF `linux`, Unity)
├── qemu/                # QEMU smoke-test subproject (ESP32-S3)
├── Dockerfile           # Dev container: ESP-IDF v6.1 + non-root host-mapped user
├── docker-compose.yml   # `dev` service with serial (/dev) passthrough
├── .devcontainer/       # CLion / VS Code Dev Container definition (reuses compose)
├── scripts/dev.py       # Container task runner (build/flash/test/format/shell)
├── scripts/test_dev.py  # Host unit tests for dev.py (`dev.py self-check`)
├── scripts/test_qemu.py # Driver for the QEMU smoke test (`test-qemu`)
├── scripts/dev.py-completion.bash  # Bash tab-completion for scripts/dev.py
├── build_root/          # Root aggregator CMake build tree (git-ignored)
├── cmake-build-esp32s3/ # CLion's own CMake build tree (git-ignored; see .idea/cmake.xml)
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
| `qemu/` (smoke test) | `qemu/build_esp32s3/` | esp32s3 | `esp32_sensors_qemu.bin` (booted under QEMU) |
| `test/` (host tests) | `test/build_linux/` | linux | `esp32_sensors_tests.elf` (run on the host) |
| `espidf-cpp-test/` (wrapper tests) | `espidf-cpp-test/build_linux/` | linux | `esp32_sensors_espidf_cpp_tests.elf` (run on the host) |

The aggregate (root) project configures into `build_root/`. Each aggregate
target is prefixed with its subproject name (`mcgowan_build`, `qemu_build`,
`test_build`) so it cannot collide with another target in the project, with
names CMake/ESP-IDF define themselves (e.g. `test`), or with a directory name.
`scripts/dev.py build` drives the aggregate project; `scripts/dev.py build
mcgowan` builds just the firmware. `mcgowan/` and `qemu/` target ESP32-S3;
`test/` targets `linux`. All build trees are git-ignored.

**Why `mcgowan/main/` and `mcgowan/src/`.** ESP-IDF requires the component that
defines `app_main` to be named `main`, so `mcgowan/main/` is that (near-empty)
component; it compiles the sources from `mcgowan/src/` and exposes
`mcgowan/src/include`. Application code lives in `mcgowan/src/`.

**Key files:**
- `Specification.md` — the single source of truth for hardware, modes, RTC memory layout, payload format, and power budget. Any implementation must conform to it.
- `mcgowan/main/CMakeLists.txt` — the firmware's component definition: lists all sources under `mcgowan/src/` and the IDF component `REQUIRES` (hal_board, nvs_flash, esp_wifi, esp_https_server, esp_http_client, esp_adc, esp_driver_gpio, mbedtls, …). **Add new sources here** or they will not compile.
- `mcgowan/sdkconfig.defaults` — the committed configuration intent (target, C++ exceptions/RTTI off, partition table, `-Os`). `mcgowan/sdkconfig` is generated from this and is **not** tracked.
- `CMakeLists.txt` (root) — the generator-agnostic aggregator: one prefixed target per subproject invoking `idf.py -C <sub> -B <sub>/<build-dir>`. `-DSUBPROJECTS="mcgowan;qemu;test;espidf-cpp-test"` (default: all) selects what is built.
- `Dockerfile` / `docker-compose.yml` — the reproducible ESP-IDF build/flash environment. Extend these (not ad-hoc `docker run`) when adding tooling. The compose `dev` service is also the target of the Dev Container (see below).
- `.devcontainer/devcontainer.json` — the CLion / VS Code **Dev Container** definition. It **reuses the compose `dev` service** (it does not define its own image), so CLion builds/starts the same container the CLI uses. Edit `docker-compose.yml` for image/mount/user changes; edit `devcontainer.json` only for IDE-level wiring.
- `scripts/dev.py` — the single container task runner (`build`, `flash`, `monitor`, `menuconfig`, `test`, `test-linux`, `test-qemu`, `self-check`, `format`, `format-check`, `format-check-staged`, `install-hooks`, `shell`). CLion run configurations and the docs both call it, so `docker compose` specifics live in one place. `shell` is the preferred way to run a container command from the host: with no arguments it opens an interactive login shell, and with arguments it runs them in the container (e.g. `./scripts/dev.py shell ls -1d /`). The pre-commit hook (`.githooks/pre-commit`) delegates its check to `scripts/dev.py format-check-staged`. An internal `complete` command lists subcommands matching a prefix (used only by the completion script).
- `scripts/test_dev.py` — host-side unit tests for `dev.py` itself (stdlib `unittest`, run by `scripts/dev.py self-check`). Imports `dev.py` in-process and covers its pure helpers — chiefly the command line `shell` builds (so shell syntax like pipes/semicolons is passed through verbatim) and `shell -C` path resolution.
- `scripts/dev.py-completion.bash` — bash tab-completion for `scripts/dev.py`. Source it to complete subcommands (e.g. `./scripts/dev.py form<TAB>` → `format`, `format-check`, `format-check-staged`). It asks `dev.py complete <prefix>` for candidates, so the command list is defined in exactly one place.
- `.idea/cmake.xml` / `.idea/runConfigurations/*.xml` — **committed** shared CLion configuration. `cmake.xml` defines the CMake profile used inside the Dev Container; the run configurations drive the build, test, format, flash, and shell tasks (six via `python3 scripts/dev.py`, one native CMake config for build/refactor). `.idea/workspace.xml` (local, git-ignored) holds per-machine state.
- `LICENSE` — GPL-3.0.
- `RELEASE_NOTES.md` — human-readable release notes; keep the "Implemented" / "Not yet implemented" split honest as milestones land.

**`mcgowan/src/` module map:**

| Area | Files |
|---|---|
| Entry point | `main.cpp` (`extern "C" void app_main`), `constants.hpp`, `mode.hpp` |
| Mode switching | `main.cpp` maps `hal::Switch` pin states to `Mode`; `led_manager.cpp` |
| Sensors | `onewire_bus.cpp` (bit-banged 1-Wire), `ds18b20.cpp`, `sensor_manager.cpp`, `sensor.hpp` |
| RTC state / buffer | `rtc_state.cpp` (`RTC_DATA_ATTR` state block), `rtc_buffer.cpp` (FIFO) |
| Power / battery | `battery_monitor.cpp` |
| Config | `app_config.cpp` (validation), `app_config_store.cpp` (NVS) |
| Configure mode | `configure_mode.cpp`, `wifi_ap.cpp`, `https_server.cpp`, `config_page.cpp`, `certs/self_signed_cert.cpp` |
| Run mode | `run_mode.cpp` (stubbed network path), `ntp_server.cpp` |
| Payload | `payload.cpp`, `temp_format.cpp`, `rom_id.cpp` (ROM-ID hex) |
| Task | `led_task.cpp` (LED task, built on `espidf_cpp::Task`) |

**HAL components (`components/`).** Hardware-specific drivers live in reusable
ESP-IDF components rather than in `mcgowan/src/`, so host tests and QEMU can swap them
for deterministic stubs. The HAL is split into one shared component plus two
**mutually exclusive** implementations:

| Component | Kind | Linked by | Provides |
|---|---|---|---|
| `hal_common` | header-only | everything (firmware, QEMU, tests) | shared vocabulary: types + concepts |
| `hal_board` | `.cpp` | production firmware (`mcgowan/`) | real, GPIO-backed drivers |
| `hal_stub` | `.cpp` | QEMU project (`qemu/`), host tests (`test/`) | pin-free, software-driven stubs |

`hal_common` is **implementation-free**: it defines the vocabulary but no
concrete `Switch` class, so both implementations can require it without
duplication. `hal_board` and `hal_stub` are mutually exclusive — a project
links exactly one.

The interface for each HAL device is a header pair: the shared vocabulary in
`hal_common` (e.g. `hal/switch_types.hpp`) plus a per-implementation header of
the same name (e.g. `hal/switch.hpp`) that includes it and adds the concrete
class. For the 3-position mode switch:

- `hal/switch_types.hpp` (`hal_common`) defines `thermo::hal::PinState`,
  `thermo::hal::Pins`, and the `thermo::hal::SwitchLike` concept — the single
  definition of the shared types.
- `hal/switch.hpp` (`hal_board` and `hal_stub`, one each) defines
  `thermo::hal::Switch` with an identical public interface (same ctor, inline
  `create()` factory, `init()`, `read()`). Each `static_assert`s
  `SwitchLike<Switch>`, so the two implementations cannot drift from the shared
  contract (or from each other).

The HAL is **semantics-free**: `Switch::read()` returns the raw `Pins` (one
`PinState` per sense pole), and mapping pin states to Configure / Off / Run is
the caller's job (`mcgowan/src/main.cpp`). Reporting both poles lets the caller detect
bad hardware (both-pins-on / both-pins-off), which the switch stub can simulate
via `set_pins()`.

Because `hal_board` and `hal_stub` would otherwise be discovered together (both
live under the project's default `components/` search path), each project
excludes the *other* via `EXCLUDE_COMPONENTS` in its top-level `CMakeLists.txt`
(set **before** `project()`): the firmware excludes `hal_stub`; the QEMU project
adds `components/` to `EXTRA_COMPONENT_DIRS` and excludes `hal_board`; the host
test project also adds `components/` to `EXTRA_COMPONENT_DIRS` (its own project
root is `test/`, so the repo-root `components/` is not on the default path) and
excludes `hal_board`.
`hal_common` is shared and is never excluded.

**`espidf_cpp` component (`components/espidf_cpp`).** Header-only C++ wrappers
over the ESP-IDF/FreeRTOS C APIs, in namespace `thermo::espidf_cpp`:

| Header | Provides |
|---|---|
| `espidf_cpp/task.hpp` | `Task<Runner>` (typed `[[noreturn]]` task body), `TaskParams` |
| `espidf_cpp/one_message_queue.hpp` | `OneMessageQueue<T>` (single-slot, trivially-copyable messages) |
| `espidf_cpp/handle.hpp` | `HandleDeleter`, `UniqueTaskHandle`, `UniqueQueueHandle` |
| `espidf_cpp/static_vector.hpp` | fixed-capacity, inline-storage vector |
| `espidf_cpp/static_string.hpp` | fixed-capacity, NUL-terminated string |

The firmware requires this component (`REQUIRES espidf_cpp`). Its host tests
live in `espidf-cpp-test/` (e.g. `test_one_message_queue.cpp`,
`test_static_vector.cpp`, `test_static_string.cpp`, plus fork+exec `_assert`
programs for the abort paths); `task.hpp` needs a real scheduler and is
verified on target by the firmware build.

**Host-testable modules.** `temp_format.cpp`, `payload.cpp`, `app_config.cpp`,
`rom_id.cpp`, and `rtc_buffer.cpp` are deliberately free of chip-specific
ESP-IDF dependencies so they build for the `linux` target and are covered by
`test/`. The HAL's `hal_stub` component (the pin-free switch) is likewise built
for the `linux` target and covered by `test/main/test_hal_switch.cpp`. **Keep it
that way** — put pure logic in these files and hardware/IDF calls behind them.

**Not yet present (expected next):**
- On-target tests for hardware-dependent code (1-Wire, deep-sleep, Wi-Fi).

**Entry point:** In ESP-IDF, `app_main(void)` is the firmware entry point and must be declared with C linkage:
```cpp
extern "C" void app_main(void);
```

## Development Workflow

> **Important:** All builds run inside the ESP-IDF Docker environment. The image is defined by the committed `Dockerfile` + `docker-compose.yml`; use `docker compose run --rm dev <cmd>` rather than ad-hoc `docker run`. `sdkconfig` is generated (git-ignored) — edit `sdkconfig.defaults` instead when changing configuration intent.

### Development environment setup

All builds run inside the official ESP-IDF Docker image to guarantee a reproducible toolchain (ESP-IDF v6.1, Xtensa GCC 15.2.0, CMake, Ninja, Python venv). The environment is defined by the committed `Dockerfile` + `docker-compose.yml`:

```bash
# From the repository root, build the dev image (first time or after Dockerfile edits)
docker compose build

# Interactive shell with the ESP-IDF environment auto-sourced
docker compose run --rm dev
```

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

The container user is mapped to your host UID/GID, so build artifacts are not root-owned, and the `dialout` group is joined for serial access. Inside the container, ESP-IDF is already on the environment; if needed, source it manually:

```bash
. $IDF_PATH/export.sh
```

### CLion: work inside the container

CLion 2026.2 is wired to the **same** compose `dev` service through a Dev
Container, so the IDE builds/starts the container automatically ("starts if
necessary") and the toolchain, CMake, Ninja, and the ESP-IDF Python venv all run
**inside** it. No host-side ESP-IDF install is required.

- **Definition:** `.devcontainer/devcontainer.json` (reuses `docker-compose.yml`;
  it never defines its own image).
- **Connect:** open the repo in CLion and accept the Dev Container prompt, or use
  *Remote Development → Dev Containers → docker-compose.yml → `dev`*. CLion
  (re)builds the image if the `Dockerfile` changed and starts the service on demand.
- **Toolchain/CMake:** select the container as the remote host (SSH into the Dev
  Container). CLion configures and builds under its **own** CMake build
  directory, `/project/cmake-build-esp32s3` (from `.idea/cmake.xml`), so the IDE
  and `idf.py` never share a CMake cache. `mcgowan/build_esp32s3/` remains the
  canonical `idf.py` build used by `scripts/dev.py build`/`flash`.

> Editing the image, mounts, or user? Change `docker-compose.yml` (and `Dockerfile`),
> **not** `devcontainer.json`. The Dev Container deliberately inherits everything
> from the compose service so the CLI and the IDE cannot drift apart.

#### Run configurations

Seven shared run configurations are committed under `.idea/runConfigurations/`
(and mirrored in the local `.idea/workspace.xml`). Six call `scripts/dev.py`,
so a single script is the source of truth for container commands — run them
from the CLion Run/Debug toolbar. The seventh is a native CLion **CMake** run
configuration used for building and refactoring inside the IDE:

| Run configuration | Kind | What it does |
|---|---|---|
| esp32_sensors (container CMake) | CMake | Builds the `esp32_sensors` CMake target in the container; enables CLion Build/Compile and C++ **refactorings** |
| Build firmware (container) | `scripts/dev.py build` | `idf.py -C mcgowan -B mcgowan/build_esp32s3 build` (and the other subprojects) |
| Flash & monitor (container) | `scripts/dev.py flash` | `idf.py -C mcgowan -B mcgowan/build_esp32s3 -p $ESP32_PORT flash monitor` (default `/dev/ttyUSB0`) |
| Host unit tests (container) | `scripts/dev.py test` | runs both suites (below); use `test-linux` / `test-qemu` to run one |
| clang-format check (container) | `scripts/dev.py format-check` | `clang-format --dry-run --Werror` |
| menuconfig (container) | `scripts/dev.py menuconfig` | Kconfig UI |
| Container shell (ESP-IDF) | `scripts/dev.py shell` | login shell with the IDF env sourced (accepts trailing args to run a command) |

Set `ESP32_PORT` (e.g. `/dev/ttyACM0`) to target a different serial device.

> **Building is container-only.** The root `CMakeLists.txt` and every subproject
> refuse to configure unless `DOCKER_IMAGE=esp32_sensors` (set by the `Dockerfile`), so a
> host invocation fails fast. The CMake profile in `.idea/cmake.xml` and the
> "esp32_sensors (container CMake)" run configuration both use the Dev
> Container toolchain, so CLion builds and refactors through the container.

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
|both|build [SUBPROJECT...]|Build all subprojects via the root aggregator, or just the named ones (mcgowan, qemu, test, espidf-cpp-test)|
|both|build-espidf-cpp-test|Build the espidf_cpp unit tests for the ESP-IDF `linux` target|
|both|test|Run both test suites: the host unit tests then the QEMU smoke test|
|both|test-linux|Build and run the host unit tests (ESP-IDF `linux` target)|
|both|test-qemu|Build and boot the QEMU smoke test (ESP32-S3 image)|
|both|self-check|Run the host unit tests for dev.py itself (stdlib `unittest`)|
|both|flash|idf.py flash monitor|
|both|monitor|idf.py monitor|
|both|menuteconfig|idf.py menuconfig|
|both|format|clang-format all source code|
|both|format-check|Check if code is formatted, non-zero exit if anything is unformatted |
|both|format-check-staged|Check if staged code is formatted, non-zero exit if anything is unformatted |
|both|shell|Run a bash command in the container (arguments after `shell` are executed); with no arguments, an interactive login shell|
|both|complete|Print subcommands matching a prefix (internal; used by bash completion)|
|host|image|Create the docker image|
|host|install-hooks|install git commit hooks|

### Shell completion

`scripts/dev.py-completion.bash` provides bash tab-completion for the
subcommands. Enable it for the current shell or permanently via `~/.bashrc`:

```bash
source scripts/dev.py-completion.bash
```

Then `./scripts/dev.py form<TAB>` completes to `format`, `format-check`,
`format-check-staged`. The script delegates candidate lookup to
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

There are two test suites, both runnable from the host via `scripts/dev.py`:

- **Host-based unit tests** live in `test/` and run on the ESP-IDF `linux`
  target with Unity — no hardware needed. They cover the pure-logic modules
  (`temp_format`, `payload`, `app_config` validation, `rom_id`, `rtc_buffer`) and
  the `hal_stub` components (`test_hal_switch.cpp` exercises the pin-free mode
  switch: `create()`/`init()`, the default reading, `set_pins()` round-trips, and
  bad-hardware simulation).
- **QEMU smoke test** (`qemu/`) builds the ESP32-S3 image and boots it under
  QEMU, then checks the boot log for expected output — no hardware needed.
- **On-target tests** are planned for hardware-dependent code (1-Wire/DS18B20
  reads, deep-sleep wake, Wi-Fi upload).
- Keep hardware interactions behind thin interfaces so business logic remains
  host-testable. When adding pure logic, put it in a host-testable translation
  unit and add it to both `mcgowan/main/CMakeLists.txt` and `test/main/CMakeLists.txt`.

```bash
# Run both suites (host unit tests, then the QEMU smoke test).
./scripts/dev.py test

# Run just one suite.
./scripts/dev.py test-linux    # host Unity tests (linux target, build_linux/)
./scripts/dev.py test-qemu     # build + boot the ESP32-S3 image under QEMU

# Run the host-side unit tests for dev.py itself (no container/hardware).
./scripts/dev.py self-check
```

Every command exits non-zero if its suite fails, so all three are CI-friendly.
The host test binary prints Unity `PASS`/`FAIL` lines; the QEMU driver prints a
`==> PASS`/`==> FAIL` summary line.

**`self-check`** runs `scripts/test_dev.py` — host-side `unittest` tests for
`dev.py`'s own pure helpers (notably the `shell` command line it builds, so
shell syntax like pipes and semicolons is passed through verbatim, and the
`shell -C DIR` path resolution). It needs no container, hardware, or
third-party dependency and works on the host and inside the container alike.

### Lint and format

Formatting is governed by the committed **`.clang-format`** (Google base, 4-space
indent, 80 columns). The existing sources already conform, so a format run is a
no-op on clean code — keep it that way and format before committing.

**Always run clang-format via the container task runner**, which formats all
`.hpp` and `.cpp` source files under `mcgowan/`, `components/`, `test/`, and
`qemu/`:

```bash
# Apply clang-format to every .hpp and .cpp source file.
scripts/dev.py format

# Check only (non-zero exit if anything is unformatted) — CI-friendly.
scripts/dev.py format-check
```

The Espressif toolchain ships a compatible `clang-format` (20.x) inside the
container. Note `.clang-format` sets `Standard: c++20` — clang-format has no
C++26 token set, and the codebase uses no C++23+ *syntax* that would confuse it.

- **Static checks:** clang-tidy via `idf.py` or standalone; ensure C++26 compatibility with the ESP-IDF toolchain.

### Conventions to follow

- Target **ESP-IDF v6.1** APIs only.
- Use **C++26** with GNU extensions; do not introduce the C-only style where C++ is appropriate.
- Declare `app_main` with `extern "C"` linkage.
- Use constexpr and noexcept as much as possible
- Treat RTC-memory structures carefully: they must be **trivially copyable / `POD`-like** with stable layout and `RTC_DATA_ATTR` placement, and must survive both deep sleep and reboots.
- Conform payload format, LED semantics, and RTC memory layout exactly to `Specification.md`.
- Keep power draw within the hourly budget defined in the spec (batch uploads, deep sleep between samples).
- **Fail loudly, not silently.** When a precondition or capacity limit is violated, `assert()` (or `abort()`) instead of silently truncating, clamping, or dropping data. Silent fallbacks hide bugs; `assert()` in this firmware is fatal and makes the contract explicit. Only use a bounded/tolerant path (e.g. a `try_*` API or an explicit early `return`) when the recovery is deliberate and the caller is expected to handle it.
- ** asserts can be tested.** For classes that are being tested on linux that have an assert test the the assert is triggered. 
Assert tests should be in a stand alone program whose name ends with "_assert".  The assert program should take as a command line parameter the name of the test case to execute, if there is no name, or if the name is invalid the program should exit with 0. 

The test case should fork and exec the "_assert" program with the name of the test case, wait for the child, and confirm that it was aborted.


### Agent interaction

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
- When the current work item is complete and no further input is expected,
  output `DONE!` as the final line of your response in the Continue (`cn`)
  CLI. Do **not** print `DONE!` if there are follow-up questions, pending
  sub-tasks, or work awaiting user confirmation.
