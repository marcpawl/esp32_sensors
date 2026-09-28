# Repository Overview

## Project Description

This repository contains the **Software Specification** for an **ESP32 Solar-Powered Thermometer Logger** (currently at Rev 12, status "Ready for implementation").

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
| Toolchain | GCC 15.1.0 (bundled with ESP-IDF v6.1) |
| License | GPL-3.0 |

> **Note:** This is a **specification-stage repository**. No firmware source, `CMakeLists.txt`, `sdkconfig`, or test code exists yet. `Specification.md` is the authoritative source of truth for all implementation work. The `install.sh` at the repo root is an unrelated Continue CLI installer artifact and is **not** part of the firmware.

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
├── Specification.md   # Authoritative software spec (Rev 12) — read this first
├── LICENSE            # GNU General Public License v3.0
└── .idea/             # CLion project metadata (empty module; no build config yet)
```

**Key files:**
- `Specification.md` — the single source of truth for hardware, modes, RTC memory layout, payload format, and power budget. Any implementation must conform to it.
- `LICENSE` — GPL-3.0.

**Not yet present (expected once implementation begins):**
- `CMakeLists.txt` / `main/CMakeLists.txt` — ESP-IDF build definitions.
- `main/*.cpp` / `main/*.hpp` — firmware sources, including `app_main`.
- `sdkconfig` / `sdkconfig.defaults` — ESP-IDF configuration.
- `components/` — reusable ESP-IDF components.
- `test/` — unit/host tests.

**Entry point:** In ESP-IDF, `app_main(void)` is the firmware entry point and must be declared with C linkage:
```cpp
extern "C" void app_main(void);
```

## Development Workflow

> **Important:** The build, flash, and test commands below are the intended workflow derived from the specification's stated environment. No build files exist yet, so these will apply once implementation is committed.

### Development environment setup

All builds run inside the official ESP-IDF Docker image to guarantee a reproducible toolchain (GCC 15.1.0, CMake, Ninja, Python venv):

```bash
# From the repository root, start an interactive container with the repo mounted
docker run --rm -it -v "$PWD":/project -w /project espressif/idf:release-v6.1
```

Inside the container, ESP-IDF environment variables are pre-set. If not, source them:

```bash
. $IDF_PATH/export.sh
```

### Build

```bash
# Inside the container
idf.py set-target esp32        # first time only
idf.py build
```

C++26 (`-std=gnu++26`) is the ESP-IDF v6.1 default for chip targets — no extra compiler flags are required.

### Flash and monitor

```bash
idf.py -p /dev/ttyUSB0 flash monitor
```

Replace `/dev/ttyUSB0` with the actual serial device. Exit the monitor with `Ctrl+]`.

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
