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

ESP-IDF v6.1 compiles C++ code using **C++26 with GNU extensions** (`-std=gnu++26`) by default for chip targets. The toolchain includes GCC 15.1.0, which is required for ESP-IDF v6.0 and later. No per-component compiler flag is needed to enable C++26 — it is the default standard.

For components that mix C and C++, ensure `app_main` is declared with C linkage:

```cpp
extern "C" void app_main(void);

