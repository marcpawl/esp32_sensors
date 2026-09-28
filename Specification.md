# Software Specification: ESP32 Solar-Powered Thermometer Logger

**Version:** Rev 12
**Status:** Ready for implementation
**Date:** 2026-09-27

A solar-powered, battery-backed ESP32 that samples three DS18B20 thermometers on a shared 1-Wire bus and uploads readings to a remote URL over WiFi at a configurable cadence. Behavior is controlled by a 3-position ON-OFF-ON switch (Configure / Off / Run). Between samples the device uses **deep sleep** to minimize power; the sample buffer and Wi-Fi connection cache live in **RTC memory** so they survive sleep and backoff reboots.

---

## Table of Contents

1. [Development Environment](#1-development-environment)
2. [Hardware](#2-hardware)
3. [Operating Modes](#3-operating-modes)
4. [Sample Buffer (RTC Memory Only)](#4-sample-buffer-rtc-memory-only)
5. [Configuration](#5-configuration)
6. [HTTPS Server (Configure Mode)](#6-https-server-configure-mode)
7. [LED Semantics](#7-led-semantics)
8. [RTC Memory Layout](#8-rtc-memory-layout)
9. [Payload Format](#9-payload-format)
10. [Power Budget — Hourly, Batched, Deep Sleep](#10-power-budget--hourly-batched-deep-sleep)
11. [Open Questions](#11-open-questions)
12. [Change History](#12-change-history)

---

## 1. Development Environment

| Item | Value |
|---|---|
| Host OS | Linux Mint |
| IDE | CLion 2026.2 |
| Container runtime | Docker |
| Container image | `espressif/idf:release-v6.1` |
| Source language | **C++26** (GNU extensions, `-std=gnu++26`) |
| Target framework | ESP-IDF v6.1 |

### Why This Image

The `espressif/idf` Docker image is the officially supported build environment for ESP-IDF, containing the cross-compiler toolchain, CMake, Ninja, Python virtual environment, and all required packages for the specified ESP-IDF version. The `release-v6.1` tag tracks the `release/v6.1` branch of ESP-IDF, which receives ongoing bug-fix updates and is considered production-ready.

### C++26 Configuration

ESP-IDF v6.1 compiles C++ code using **C++26 with GNU extensions** (`-std=gnu++26`) by default for chip targets. The toolchain includes GCC 15.2.0 (Xtensa `esp-15.2.0_20251204`), which is required for ESP-IDF v6.0 and later. No per-component compiler flag is needed to enable C++26 — it is the default standard.

For components that mix C and C++, ensure `app_main` is declared with C linkage:

```cpp
extern "C" void app_main(void);
```

### Container Usage

**Build (non-interactive):**
```bash
docker run --rm -v $PWD:/project -w /project -u $UID \
  -e HOME=/tmp \
  espressif/idf:release-v6.1 idf.py build
```

**Interactive session (for menuconfig, debugging):**
```bash
docker run --rm -v $PWD:/project -w /project -u $UID \
  -e HOME=/tmp -it \
  espressif/idf:release-v6.1
# Inside the container:
idf.py menuconfig
idf.py build
```

**Flashing and monitoring (Linux host):**

```bash
docker run --rm -v $PWD:/project -w /project -u $UID \
  -e HOME=/tmp \
  --device=/dev/ttyUSB0 \
  espressif/idf:release-v6.1 idf.py -p /dev/ttyUSB0 flash monitor
```

> **Docker note (Linux):** Your user must have access to the Docker daemon (typically via the `docker` group) and to the serial device (via the `dialout` group). Add with:
> ```bash
> sudo usermod -aG docker,dialout $USER
> ```
> then log out and back in.

**Git ownership workaround:**

```bash
docker run --rm -v $PWD:/project -w /project -u $UID \
  -e HOME=/tmp -e IDF_GIT_SAFE_DIR='/project' \
  espressif/idf:release-v6.1 idf.py build
```

### CLion Configuration Notes

**Docker toolchain:** CLion's Docker toolchain integration is first-class and officially supported. Configure it under **Settings | Build, Execution, Deployment | Toolchains → Add → Docker**.

- Point CLion at the standard Docker socket (`unix:///var/run/docker.sock`).
- Select the `espressif/idf:release-v6.1` image.
- CLion will build and run commands inside the container transparently.

**ESP-IDF environment in CLion:** Inside the Docker container, ESP-IDF's environment is already sourced by the image entrypoint, so the environment-file mechanism is typically unnecessary. If CLion complains about missing `idf.py`, point its "Environment file" setting at `/opt/esp/idf/export.sh` (path inside the image) or leave it blank and rely on the container's entrypoint.

**Serial monitor in CLion:** Pass the serial device through to the container with `--device=/dev/ttyUSB0` (add this to the CLion Docker toolchain's container options). Alternatively, use CLion's built-in terminal to run `idf.py monitor`.

**Recommended workflow:** Use CLion for editing, code navigation, and CMake configuration; use CLion's built-in terminal (`Alt+F12`) for `idf.py` operations (build, flash, monitor, menuconfig).

### C++26 Feature Notes for Embedded

| Feature | Status on ESP-IDF |
|---|---|
| Coroutines | Supported by GCC 15, but avoid heavy use — stack consumption is hard to bound on embedded |
| Concepts / `requires` | Fully supported; useful for compile-time interface constraints |
| `std::expected` | Likely available with GCC 15's libstdc++; verify in-container |
| Modules (`.cppm`) | **Avoid.** ESP-IDF's build system is header-based; C++ modules are not integrated with `idf_component_register` |
| Exceptions / RTTI | Disabled by default in ESP-IDF for code-size reasons; enable explicitly in `menuconfig` if needed |
| `std::format` | Heavy; prefer ESP-IDF's `ESP_LOGI` for logging |

**Recommended practice:** Use C++26 where it improves safety and clarity (concepts, `constexpr`, `std::array`, `std::span`, `std::optional`), but keep code compatible with embedded constraints. Avoid features that pull in large runtime support.

### C/C++ Interop

ESP-IDF is C-first. All ESP-IDF APIs are C functions and structs. Key interop rules for C++:

1. `app_main` must be `extern "C"`.
2. FreeRTOS types (`TaskHandle_t`, `QueueHandle_t`, etc.) are opaque C pointers — safe to store in C++ classes.
3. Designated initializers follow **C++ rules**, not C rules. C++26 prohibits out-of-order designators, nested designators, mixing designated and regular initializers, and designated array initialization. When adapting ESP-IDF C examples, restructure these initializers.
4. Prefer `constexpr` and `static_assert` over macros where C++ equivalents exist.

### Build System Integration (C++)

- Create a `main` component (or a custom component) with `CMakeLists.txt`.
- Register sources with `idf_component_register()`.
- Declare component dependencies explicitly (`REQUIRES` or `PRIV_REQUIRES`).
- For C++ source files, no special flag is needed for C++26; it is the default.

Example `main/CMakeLists.txt`:

```cmake
idf_component_register(
    SRCS "main.cpp" "sensor.cpp" "config.cpp"
    INCLUDE_DIRS "include"
    REQUIRES esp_wifi nvs_flash driver esp_http_client
)
```

---

## 2. Hardware

### 2.1 Components

| Component | Qty | Notes |
|---|---|---|
| ESP32 dev board | 1 | Any ESP32 with WiFi |
| DS18B20 sensors | 3 | Shared 1-Wire bus |
| 4.7 kΩ resistor | 1 | 1-Wire pull-up to 3.3 V |
| Red LED | 1 | + current-limiting resistor |
| Green LED | 1 | + current-limiting resistor |
| DPDT ON-OFF-ON switch | 1 | Recommended |
| Solar panel | 1 | Sized per §10 |
| Battery | 1 | Li-ion or LiFePO4 |
| Charge controller | 1 | **TBD** — see §11.1 |
| 3.3 V regulator | 1 | Low quiescent current |
| Voltage divider | 2 resistors | Battery ADC (e.g., 100 kΩ / 100 kΩ) |
| P-MOSFET (optional) | 1 | Gates divider to save ~21 µA (§2.6) |

### 2.2 Power Design

```
Solar → Charge controller (TBD) → Battery → Switch → Regulator → ESP32
```

- Center switch position cuts all power to the regulator and ESP32.
- Charge controller keeps charging the battery regardless of switch position.
- Switch must handle ~500 mA peak.

### 2.3 Switch Wiring — DPDT ON-OFF-ON

| Pole | Function |
|---|---|
| Pole 1 | Battery + → regulator input (power gating) |
| Pole 2 | Mode signal to GPIO |

| GPIO | Configure | Center | Run |
|---|---|---|---|
| GPIO 18 (CFG) | HIGH | LOW | LOW |
| GPIO 19 (RUN) | LOW | LOW | HIGH |

### 2.4 GPIO Assignment

| GPIO | Function |
|---|---|
| GPIO 4 | 1-Wire bus (DS18B20 ×3 + 4.7 kΩ pull-up) |
| GPIO 16 | Red LED |
| GPIO 17 | Green LED |
| GPIO 18 | Mode sense: Configure |
| GPIO 19 | Mode sense: Run |
| GPIO 34 | Battery voltage ADC (via divider) |
| GPIO 25 (optional) | Divider gate (P-MOSFET control) |

> **GPIO 34 note:** input-only. Choose a divider keeping the ADC input below ~2.5 V at max battery voltage.

### 2.5 Battery Voltage Measurement

- ADC pin: GPIO 34.
- Divider: `R_top` from battery + to ADC, `R_bottom` from ADC to GND.
- `V_adc = V_batt × R_bottom / (R_top + R_bottom)`.
- Firmware multiplies by `(R_top + R_bottom) / R_bottom`, then applies calibration gain/offset.
- 16-sample average, discard high/low.
- **Optional gating:** drive GPIO 25 low to energize the divider just before an ADC read, then high to disconnect it. Saves ~21 µA continuously.

### 2.6 Divider Quiescent Draw

A 100 kΩ / 100 kΩ divider draws ~21 µA at 4.2 V. In deep sleep, this is comparable to the ESP32's ~10 µA — **gating is strongly recommended** for the deep-sleep path. If gating is used, the divider is only energized during the ADC read (a few ms), and its average contribution becomes negligible.

---

## 3. Operating Modes

### 3.1 Switch Center — OFF
No power to ESP32. Battery continues charging.

### 3.2 Switch Left — CONFIGURE MODE

1. Both LEDs ON (solid).
2. WiFi **AP** mode (credentials from NVS, factory default first boot).
3. **HTTPS server** on port 443.
4. Config page at `/`.

### 3.3 Switch Right — RUN MODE

On boot (which occurs on every deep-sleep wake), firmware detects Run.

1. Both LEDs briefly ON (self-test), then off.
2. Read battery voltage; initialize / evaluate the power state machine (§3.6).
3. Read all three DS18B20s (°C) — **no bus scan needed**; ROM IDs are cached in RTC (§8).
4. Append a sample record (age + battery + 3 temps) to the **RTC buffer** (§4).
5. If buffer ≥ batch size **and** power state allows transmission → upload (§3.5).
6. Otherwise → deep sleep until next sample.
7. On upload failure → exponential backoff via RTC memory (§3.7); Wi-Fi cache is **cleared** on comms failure (§3.10).
8. Between wake events → **deep sleep** (§3.8).

### 3.4 Sampling Loop (Run Mode)

Each deep-sleep wake is effectively a reboot; state is restored from RTC memory before the cycle proceeds.

1. Read battery voltage (mV).
2. Read all three DS18B20s (°C) using cached ROM IDs (no bus enumeration).
3. Append sample to RTC buffer.
4. If buffer ≥ batch size **and** TX allowed → upload (§3.5).
5. Else → deep sleep for `interval_s`.

### 3.5 Batch Upload

Configurable **Batch Size N** (integer ≥ 1): 1 = every sample; 24 = daily.

When the buffer reaches **N** and power permits:

1. Green LED ON.
2. Connect to Station WiFi using the **cached connection parameters** (§3.9) if valid, else a full connect.
3. POST batched JSON (§9). Temperatures °C one decimal; battery mV integer.
4. On success:
   - Green LED 200 ms pulse.
   - Clear RTC buffer.
   - `fail_count = 0`.
   - Update Wi-Fi cache (BSSID, channel, IP) in RTC memory.
   - Deep sleep until next sample.
5. On failure → §3.7, and clear Wi-Fi cache per §3.10.

### 3.6 Low-Battery Behavior — High-Water / Low-Water Hysteresis

| Threshold | Config key | Meaning |
|---|---|---|
| Low Water | `batt_low_mv` | Below → transmission disabled |
| High Water | `batt_high_mv` | Above → transmission re-enabled |

Constraint: `batt_high_mv > batt_low_mv`.

```
        batt < LOW_WATER                batt > HIGH_WATER
TX_OK ─────────────────► TX_INHIBITED ─────────────────► TX_OK
```

**Rules:**

1. On boot: `batt ≥ batt_high_mv` → **TX_OK**; else **TX_INHIBITED**.
2. In **TX_OK**, `batt < batt_low_mv` → **TX_INHIBITED**.
3. In **TX_INHIBITED**, `batt > batt_high_mv` → **TX_OK**.
4. Voltage sampled at every wake.
5. Buffered samples accumulate while inhibited; resumed batches include all buffered samples with correct `age_s`.
6. Buffer overflow → drop oldest (§4.2).
7. LED: red 500 ms blink every 10 s while inhibited.

**Defaults (Li-ion):** `batt_low_mv = 3400`, `batt_high_mv = 3700`.

### 3.7 Error Handling — Exponential Backoff via RTC Memory

On any upload failure:

1. Red LED ON.
2. `fail_count++` (RTC).
3. `backoff_s = min(retry_base_s × 2^(fail_count − 1), retry_max_s)`, defaults 60 → 3600.
4. Deep sleep `backoff_s`, then **reboot**, retry.
5. On success: `fail_count = 0`.

| Failures | Backoff |
|---|---|
| 1 | 60 s |
| 2 | 120 s |
| 3 | 240 s |
| 4 | 480 s |
| 5 | 960 s |
| 6 | 1920 s |
| 7+ | 3600 s |

> Backoff applies only to transmission failures, not low-battery inhibition.

### 3.8 Deep Sleep Between Samples

Deep sleep is used between sample events **and** between backoff retries.

```c
esp_sleep_enable_timer_wakeup(sleep_s × 1e6);
esp_deep_sleep_start();
```

| Aspect | Value |
|---|---|
| Sleep current | ~10 µA |
| RAM retained | No (regular RAM lost) |
| RTC memory retained | Yes |
| Wake behavior | Full reboot; firmware restores state from RTC |

**No bus scan on wake:** DS18B20 ROM IDs are stored in RTC memory (§8), so the 1-Wire bus is read directly by address. This saves the ~1 s enumeration cost on every wake.

### 3.9 Wi-Fi Connection Caching (RTC)

Before each deep sleep, the following are stored in RTC memory:

| Field | Size | Notes |
|---|---|---|
| `bssid` | 6 B | AP MAC address |
| `channel` | 1 B | Wi-Fi channel |
| `ip` | 4 B | Assigned IP (if static/DHCP-cached) |
| `valid` | 1 B | Cache validity flag |

On wake, firmware attempts a **fast reconnect** using cached BSSID/channel/IP. This bypasses scanning and DHCP, reducing connect time from ~7.5 s to ~0.5 s and saving ~85 % of the connect energy.

If the fast reconnect fails, firmware falls back to a full connect (scan + DHCP), and updates the cache on success.

### 3.10 Cache Invalidation on Communication Failure

**The Wi-Fi connection cache is cleared on any communication failure.**

**Cache-clear triggers:**

1. Fast reconnect fails using cached parameters.
2. Full connect (scan + DHCP) fails.
3. TCP connect to destination URL fails.
4. TLS handshake fails.
5. HTTP request times out or returns non-2xx.
6. Any DNS resolution failure.

**Cleared fields:** `bssid`, `channel`, `ip`, `valid = 0`.

**Retained across clear:** `fail_count` (for backoff sequencing), sample buffer (data is not discarded).

---

## 4. Sample Buffer (RTC Memory Only)

Regular RAM is lost on deep sleep, so the sample buffer lives **exclusively in RTC memory**.

### 4.1 Record Format

| Field | Size | Notes |
|---|---|---|
| `age_base` | 4 B | Seconds since boot at sample time |
| `batt_mv` | 2 B | Battery voltage in mV (uint16) |
| `t1`, `t2`, `t3` | 2 B each | Temperature × 100 as int16, in °C, or `0x8000` = null |

≈ 12–18 bytes per sample.

### 4.2 RTC Buffer

- ESP32 RTC slow memory: ~8 KB usable → **~450 compact samples**.
- **On overflow: drop the oldest sample** (FIFO eviction).
- Cleared after successful upload.
- Accumulates freely while in **TX_INHIBITED**.
- **Lost on power-off** (switch to center, battery removal). Acceptable per §11.2.

### 4.3 Persistence

The RTC buffer survives:

- Deep sleep wake-ups.
- Backoff reboots.
- Watchdog resets caused by firmware.

It does **not** survive:

- Switch to center (power removed).
- Battery depletion.
- Firmware reflash.

---

## 5. Configuration

### 5.1 Configure Page Sections

#### 5.1.1 Network & Upload

| Field | Type | Editable | Validation |
|---|---|---|---|
| AP SSID | string | Yes | 1–32 chars |
| AP Password | string | Yes | 8–63 chars |
| Station SSID | string | Yes | 1–32 chars |
| Station Password | string | Yes | 8–63 chars |
| Data Destination URL | string | Yes | Valid `http(s)://` URL |
| Data Collection Interval | integer seconds | Yes | > 0 |
| Error Retry Interval (base) | integer seconds | Yes | > 0 |
| Error Retry Interval (max cap) | integer seconds | Yes | ≥ base |
| Batch Size N | integer | Yes | ≥ 1 |
| Device Name | string | Yes | 1–32 chars |
| NTP Server | string | Yes | Valid hostname; default `time.nrc.ca` |
| Last Configuration Saved | string (UTC) | **No** (read-only) | Display only; set on save |

#### 5.1.2 Battery — Thresholds & Calibration

| Field | Type | Editable | Validation |
|---|---|---|---|
| Low Water (mV) | integer | Yes | > 0 |
| High Water (mV) | integer | Yes | > Low Water |
| Divider R_top (Ω) | integer | Yes | > 0 |
| Divider R_bottom (Ω) | integer | Yes | > 0 |
| ADC calibration factor | float | Yes | ~1.0 |
| ADC calibration offset (mV) | integer | Yes | default 0 |
| Divider gating enabled | bool | Yes | default false |

#### 5.1.3 Device ID → Name Mapping

| ROM ID (read-only) | Friendly Name (editable) | Current Temp |
|---|---|---|
| `28FF64A1B21603C4` | `outside` | 12.4 °C |
| `28FF3B2C7716042F` | `greenhouse` | 24.1 °C |
| `28FFAA19C31604D1` | `water_tank` | 18.7 °C |

### 5.2 NVS Storage

Namespace: `thermo_cfg`

| Key | Type | Default |
|---|---|---|
| `ap_ssid` | string | `ESP32-Thermo-XXXX` |
| `ap_pass` | string | `configme123` |
| `sta_ssid` | string | `""` |
| `sta_pass` | string | `""` |
| `dest_url` | string | `""` |
| `interval_s` | u32 | `3600` |
| `retry_base_s` | u32 | `60` |
| `retry_max_s` | u32 | `3600` |
| `batch_size` | u32 | `1` |
| `batt_low_mv` | u32 | `3400` |
| `batt_high_mv` | u32 | `3700` |
| `batt_r_top` | u32 | `100000` |
| `batt_r_bot` | u32 | `100000` |
| `batt_cal_gain` | float | `1.0` |
| `batt_cal_offset` | i32 | `0` |
| `batt_gate_en` | bool | `false` |
| `dev_name` | string | `esp32-thermo` |
| `ntp_server` | string | `time.nrc.ca` |
| `saved_time` | u32 | `0` (Unix epoch UTC) |
| `cert_pem` | blob | self-signed |
| `map_<ROMID>` | string | friendly name |

### 5.3 Save Handler — NTP Sync and `saved_time`

On `POST /save`:

1. Validate all submitted fields.
2. If validation fails, re-render the form with error messages; `saved_time` is **not** updated.
3. Persist all validated fields to NVS.
4. **Connect to the configured NTP server** and obtain the current UTC time.
5. Store the UTC time (Unix epoch) as `saved_time` in NVS.
6. Redirect to `/saved`.

**NTP failure handling:**

- Retry up to 3 times with a short timeout (e.g., 5 s each).
- If all retries fail, **do not update** `saved_time`; persist the rest of the configuration. Display a warning on `/saved`.

### 5.4 NTP Server Field

- **Default:** `time.nrc.ca`
- **User-editable:** Yes
- **Validation:** non-empty, valid hostname, no scheme prefix.
- **Usage:** Used only during the save handler (§5.3).

> **Confirmed:** `time.nrc.ca` is a valid NTP server provided by the National Research Council of Canada. NRC recommends using the hostname rather than IP.

---

## 6. HTTPS Server (Configure Mode)

| Method | Path | Purpose |
|---|---|---|
| GET | `/` | Config form + `saved_time` read-only |
| GET | `/scan` | JSON with ROM IDs, current temps, battery voltage |
| POST | `/save` | Validate + persist + NTP sync for `saved_time` |
| GET | `/saved` | Confirmation page |

### 6.1 `/scan` Response Format

```json
{
  "battery_mv": 3721,
  "unit": "C",
  "sensors": [
    { "rom": "28FF64A1B21603C4", "name": "outside",    "temp_c": 12.4 },
    { "rom": "28FF3B2C7716042F", "name": "greenhouse", "temp_c": 24.1 },
    { "rom": "28FFAA19C31604D1", "name": "water_tank", "temp_c": 18.7 }
  ]
}
```

### 6.2 Config Page `saved_time` Display

- **Label:** "Last Configuration Saved"
- **Format:** `YYYY-MM-DD HH:MM:SS UTC`
- **Initial state:** `"never"` if `saved_time == 0`.

---

## 7. LED Semantics

| State | Red | Green |
|---|---|---|
| Configure mode | ON (solid) | ON (solid) |
| Run – boot | 200 ms flash | 200 ms flash |
| Run – sampling | OFF | OFF |
| Run – uploading | OFF | ON |
| Run – upload OK | OFF | 200 ms pulse |
| Run – TX error | ON | OFF |
| Run – TX inhibited (low battery) | 500 ms blink every 10 s | OFF |
| Run – recovered (crossed high water) | OFF | 1 s solid, then OFF |
| Fatal error | Blink 0.5 Hz | OFF |

---

## 8. RTC Memory Layout

| Variable | Type | Purpose |
|---|---|---|
| `boot_count` | u32 | Diagnostics |
| `fail_count` | u8 | Consecutive upload failures |
| `tx_state` | u8 | `TX_OK` / `TX_INHIBITED` |
| `buffer` | array | Sample records |
| `buffer_len` | u16 | Valid record count |
| `next_sample_at` | u32 | Absolute time of next sample |
| `rom_ids[3]` | 8 B × 3 | Cached DS18B20 ROM IDs |
| `rom_count` | u8 | Number of cached ROM IDs |
| `wifi.bssid` | 6 B | Cached AP BSSID |
| `wifi.channel` | u8 | Cached Wi-Fi channel |
| `wifi.ip` | u32 | Cached IP address |
| `wifi.valid` | u8 | Cache validity flag |
| `last_upload_ok` | u8 | Diagnostics |

---

## 9. Payload Format

### 9.1 Batched Upload (N ≥ 1)

```json
{
  "device": "esp32-thermo",
  "unit": "C",
  "batt_unit": "mV",
  "batch_size": 24,
  "samples": [
    { "age_s": 82800, "batt_mv": 3712, "outside": 12.4, "greenhouse": 24.1, "water_tank": 18.7 },
    { "age_s": 79200, "batt_mv": 3708, "outside": 12.6, "greenhouse": 24.3, "water_tank": 18.6 },
    { "age_s":     0, "batt_mv": 3745, "outside": 13.1, "greenhouse": 24.5, "water_tank": 18.5 }
  ]
}
```

- `batt_unit: "mV"` — battery voltage integer millivolts.
- Temperatures: one decimal, round-half-away-from-zero (§9.3).
- Keys are friendly names from the mapping.
- Missing sensor → `null`.

### 9.2 Google Sheets Column Layout

| Col | Field | Source | Format |
|---|---|---|---|
| A | UTC date-time | `receive_time − age_s` | `yyyy-mm-dd hh:mm:ss` UTC |
| **B** | **Battery voltage (mV)** | `batt_mv` | integer |
| C | Thermometer 1 | first named sensor | one decimal, °C |
| D | Thermometer 2 | second named sensor | one decimal, °C |
| E | Thermometer 3 | third named sensor | one decimal, °C |

### 9.3 Temperature Serialization

- Internal: °C × 100 (int16).
- Output: one decimal, **round-half-away-from-zero**:

```
sign = (v < 0) ? -1 : 1
a    = abs(v)
q    = a / 10
r    = a % 10
if (r >= 5) q += 1
result = sign * q
```

- Emit as `%.1f` JSON numbers so `24.0` retains its trailing zero.

### 9.4 Spreadsheet Receiver Action

For each sample:

```
sample_time    = receive_time − age_s      → column A (UTC)
battery_mv     = batt_mv                   → column B
temp_1..temp_N = named sensor values       → columns C…
```

---

## 10. Power Budget — Hourly, Batched, Deep Sleep

### 10.1 Assumptions

| Parameter | Value |
|---|---|
| Collection interval | 3600 s |
| Batch size N | 24 |
| Active sample | ~2.5 s @ 40 mA |
| Active upload | ~1.5 s @ 120 mA (cached) |
| Deep sleep | ~10 µA |
| Regulator Iq | 50 µA |
| Divider Iq | 0 µA (gated) |
| Charge controller Iq | **TBD** |

### 10.2 Per-Day Energy (gated divider)

| Phase | Current | Duration/day | Charge (mAs/day) |
|---|---|---|---|
| Sample (×24) | 40 mA | 60 s | 2,400 |
| Upload (×1, cached) | 120 mA | 1.5 s | 180 |
| Deep sleep | 10 µA | 86,338 s | 863 |
| Regulator Iq | 50 µA | 86,400 s | 4,320 |
| Charge controller Iq | TBD | 86,400 s | TBD |
| **Total (excl. TBD)** | — | — | **≈7,763 mAs ≈ 2.16 mAh/day** |

**Average current:** ≈ **0.25 mA**.

### 10.3 Battery Life (No Sun)

| Battery | Deep-sleep + caching |
|---|---|
| 1000 mAh | ~460 days |
| 2000 mAh | ~925 days |
| 5000 mAh | ~2,300 days |

### 10.4 Solar Panel Sizing

Daily energy ≈ 2.16 mAh/day @ 3.7 V ≈ **8 mWh/day**. With 0.504 efficiency and 3 sun hours:

```
Panel ≈ 8 / 0.504 / 3 ≈ 5.3 mW
```

| Path | Recommended panel |
|---|---|
| Gated divider | **20–50 mW (5 V)** |
| Ungated divider | **50–100 mW (5 V)** |

---

## 11. Open Questions

1. **Charge controller** — TBD (quiescent current matters most now that everything else is ~250 µA).
2. **Buffer persistence across power-off** — acceptable to lose buffered samples on switch-to-center? (Current answer: yes.)
3. ~~Light sleep vs. deep sleep~~ — **resolved: deep sleep + caching.**
4. **Default thresholds** — `3400 / 3700 mV` correct for your chemistry?
5. ~~Battery ADC pin~~ — **resolved: GPIO 34 with divider.**
6. ~~Buffer cap during inhibition~~ — **resolved: drop oldest sample.**
7. ~~JSON number formatting~~ — **resolved: one decimal place.**
8. ~~Rounding mode~~ — **resolved: round-half-away-from-zero.**
9. **Temperature serialization form** — `%.1f` JSON numbers still recommended; confirm.
10. ~~Divider gating~~ — **resolved: gating recommended; configurable flag default false.** Confirm MOSFET in hardware.
11. **Sensor column order** — spreadsheet columns C… assigned in mapping-table order; confirm stable.
12. **Battery voltage in `/scan`** — raw mV only, or add derived percentage?
13. **Absolute scheduling** — track `next_sample_at` in RTC to avoid drift? **Recommendation: yes.**
14. **Wi-Fi cache invalidation on total failure** — also clear when `fail_count` exceeds threshold (e.g., 5)? **Recommendation: yes.**
15. ~~NTP server default confirmation~~ — **resolved: `time.nrc.ca` confirmed valid.**
16. ~~Docker vs. Podman~~ — **resolved: Docker.**

---

## 12. Change History

| Rev | Changes |
|---|---|
| 1 | Initial specification: 3 thermometers, LEDs, ON-OFF-ON switch, solar/battery, configure/run modes. |
| 2 | DS18B20 clarified as 1-Wire (not I²C). Added Station SSID/password. Config page now includes device ID → name mapping and current temperature readings. Run-mode error path: red LED → deep sleep for Error Retry Interval → reboot → re-attempt. |
| 3 | Exponential backoff via RTC memory; batch size N (≥ 1) with per-sample `age_s`; skip WiFi when battery low and buffer samples; light sleep between samples (default); charge controller left TBD. |
| 4 | Low-battery behavior replaced with high-water / low-water hysteresis state machine (`TX_OK` ↔ `TX_INHIBITED`). |
| 5 | Temperatures explicitly in **degrees Celsius**; payload includes `"unit": "C"`. |
| 6 | Buffer overflow: drop oldest sample. JSON temperature format: one decimal place. |
| 7 | Rounding mode fixed to **round-half-away-from-zero**. Serialization detail added for preserving one decimal. |
| 8 | Battery voltage added to `/scan` and JSON payload. Google Sheets column layout: A = UTC datetime, **B = battery mV**, C… = thermometers. |
| 9 | **Deep sleep adopted** (light sleep path removed). Wi-Fi connection parameters cached in RTC memory. Cache cleared on any communication failure. |
| 10 | `saved_time` field added (non-editable, UTC, set on save via NTP). NTP server field added (default `time.nrc.ca`, confirmed valid). |
| 11 | Development environment: Linux Mint, Podman, CLion 2026.2, `espressif/idf:release-v6.1`, C++26. |
| 12 | Container runtime switched from Podman to **Docker**. |
