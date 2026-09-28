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

> **Note:** Firmware source lives in `main/` and builds successfully (`esp32_sensors.bin` has been produced). `Specification.md` remains the authoritative source of truth for all implementation work; where code and spec disagree, the spec wins. The `install.sh` at the repo root is an unrelated Continue CLI installer artifact and is **not** part of the firmware.

> **Implementation status (current milestone):**
> - **Configure mode — implemented.** SoftAP, HTTPS server, config UI, NVS persistence, LED semantics.
> - **Run mode — scaffolded/stubbed.** Real: boot self-test LEDs, battery read + hysteresis, sensor read with cached ROM IDs (first-boot scan), sample append to the RTC buffer, deep sleep. **Stubbed:** Wi-Fi station connect + cached fast-reconnect (§3.9), batch upload + payload wire-out (§9, payload is formatted and logged but not sent), exponential backoff (§3.7).
> - **No tests yet.** There is no `test/` directory; host-based tests are planned.

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
├── CMakeLists.txt       # Top-level ESP-IDF project definition
├── sdkconfig.defaults   # Committed IDF defaults (sdkconfig itself is generated)
├── main/                # Firmware component (C++26) — see module map below
├── Dockerfile           # Dev container: ESP-IDF v6.1 + non-root host-mapped user
├── docker-compose.yml   # `dev` service with serial (/dev) passthrough
├── .dockerignore        # Build-context exclusions
├── .gitignore           # Excludes build/, sdkconfig, .idea/, caches
├── AGENTS.md            # This file — guidance for AI agents and developers
├── LICENSE              # GNU General Public License v3.0
├── install.sh           # Unrelated Continue CLI installer (not firmware)
└── .idea/               # CLion project metadata (git-ignored)
```

**Key files:**
- `Specification.md` — the single source of truth for hardware, modes, RTC memory layout, payload format, and power budget. Any implementation must conform to it.
- `main/CMakeLists.txt` — lists all sources and the IDF component `REQUIRES` (nvs_flash, esp_wifi, esp_https_server, esp_http_client, esp_adc, esp_driver_gpio, mbedtls, …). **Add new sources here** or they will not compile.
- `sdkconfig.defaults` — the committed configuration intent (target, C++ exceptions/RTTI off, partition table, `-Os`). `sdkconfig` is generated from this and is **not** tracked.
- `Dockerfile` / `docker-compose.yml` — the reproducible ESP-IDF build/flash environment. Extend these (not ad-hoc `docker run`) when adding tooling.
- `LICENSE` — GPL-3.0.

**`main/` module map:**

| Area | Files |
|---|---|
| Entry point | `main.cpp` (`extern "C" void app_main`), `constants.hpp`, `mode.hpp` |
| Mode switching | `mode_controller.cpp`, `led_manager.cpp` |
| Sensors | `onewire_bus.cpp` (bit-banged 1-Wire), `ds18b20.cpp`, `sensor_manager.cpp`, `sensor.hpp` |
| RTC state / buffer | `rtc_state.cpp` (`RTC_DATA_ATTR` state block) |
| Power / battery | `battery_monitor.cpp` |
| Config | `app_config.cpp`, `app_config_store.cpp` (NVS) |
| Configure mode | `configure_mode.cpp`, `wifi_ap.cpp`, `https_server.cpp`, `config_page.cpp`, `certs/self_signed_cert.cpp` |
| Run mode | `run_mode.cpp` (stubbed network path), `ntp_client.cpp` |
| Payload | `payload.cpp`, `temp_format.cpp` |

**Not yet present (expected next):**
- `test/` — host-based (Unity, `linux` target) and on-target tests.
- `.clang-format` — formatting config.
- `components/` — reusable ESP-IDF components (currently everything is in `main/`).

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

The container user is mapped to your host UID/GID, so build artifacts are not root-owned, and the `dialout` group is joined for serial access. Inside the container, ESP-IDF is already on the environment; if needed, source it manually:

```bash
. $IDF_PATH/export.sh
```

### Build

```bash
# Run any IDF command via compose
docker compose run --rm dev idf.py set-target esp32   # first time only
docker compose run --rm dev idf.py build
```

C++26 (`-std=gnu++26`) is the ESP-IDF v6.1 default for chip targets — no extra compiler flags are required.

### Flash and monitor

The host `/dev` tree is bind-mounted into the container, so plug the board in **before** starting the container and pass the port explicitly:

```bash
docker compose run --rm dev idf.py -p /dev/ttyUSB0 flash monitor
```

Replace `/dev/ttyUSB0` with the actual serial device (`/dev/ttyACM0`, etc.). Exit the monitor with `Ctrl+]`.

### Testing approach

- Prefer **host-based unit tests** for pure logic (payload formatting, buffer/backoff state machines, RTC memory layout encode/decode) using the ESP-IDF `linux` target and Unity.
- Use **on-target tests** for hardware-dependent code (1-Wire/DS18B20 reads, deep-sleep wake, WiFi upload).
- Keep hardware interactions behind thin interfaces so business logic remains host-testable.

```bash
# Host-based test run (once test scaffolding exists)
cd test && idf.py --preview set-target linux && idf.py build && ./build/project.elf
```

### Lint and format

- **Formatting:** `clang-format` (GNU/LLVM-based style); use a committed `.clang-format` once added.
- **Static checks:** clang-tidy via `idf.py` or standalone; ensure C++26 compatibility with the ESP-IDF toolchain.

### Conventions to follow

- Target **ESP-IDF v6.1** APIs only.
- Use **C++26** with GNU extensions; do not introduce the C-only style where C++ is appropriate.
- Declare `app_main` with `extern "C"` linkage.
- Treat RTC-memory structures carefully: they must be **trivially copyable / `POD`-like** with stable layout and `RTC_DATA_ATTR` placement, and must survive both deep sleep and reboots.
- Conform payload format, LED semantics, and RTC memory layout exactly to `Specification.md`.
- Keep power draw within the hourly budget defined in the spec (batch uploads, deep sleep between samples).
