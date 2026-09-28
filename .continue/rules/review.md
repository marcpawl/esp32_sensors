---
invokable: true
---

Review this code for potential issues, including:

**Spec conformance (ESP32 Solar-Powered Thermometer Logger)**
- Does the code match `Specification.md` (Rev 12) for operating modes (Configure / Off / Run), sample buffer semantics, configuration keys, HTTPS server behavior, LED semantics, RTC memory layout, and payload format?
- Are any deviations from the spec intentional and clearly justified?

**ESP-IDF and toolchain**
- Is the code targeting ESP-IDF v6.1 APIs only (no deprecated or removed APIs)?
- Is `app_main` declared with C linkage (`extern "C" void app_main(void);`) when mixing C and C++?
- Is C++26 (`-std=gnu++26`) used consistently, with no reliance on non-GNU constructs?

**RTC memory and deep sleep (high-risk)**
- Are RTC-memory structs trivially copyable with stable layout, and correctly marked (`RTC_DATA_ATTR` / `RTC_NOINIT_ATTR`)?
- Is initialization/handling correct across cold boot, deep-sleep wake, and backoff reboots?
- Can buffer indices, counters, or pointers become stale, uninitialized, or corrupt after wake?
- Are there unbounded or non-volatile-unsafe structures that will not survive sleep?

**Power budget**
- Does the sampling/upload cadence respect the hourly power budget in the spec?
- Are uploads batched rather than per-sample? Is deep sleep entered between samples?
- Any busy-waits, missing sleep calls, or peripherals left powered that increase draw?

**1-Wire / DS18B20 sensors**
- Is the shared 1-Wire bus accessed with correct timing and exclusive ownership (no concurrent access)?
- Are CRC checks, missing sensors, and read timeouts handled gracefully?
- Are three sensors enumerated and mapped deterministically?

**WiFi and upload reliability**
- Is connection caching in RTC memory used correctly?
- Are timeouts, retries, and backoff implemented and reboot-safe?
- Is the upload payload correct per the spec's payload format?
- Are credentials/secrets stored safely and not logged?

**Concurrency and memory safety (C++)**
- Any data races between tasks, ISRs, or the mode controller?
- Any buffer overflows, out-of-bounds indexing, use-after-free, or uninitialized reads?
- Are error paths and return values from I/O (1-Wire, WiFi, HTTP) checked?

**Configure-mode HTTPS server**
- Is the HTTPS server secured (proper certificates, no plaintext fallback)?
- Is configuration input validated before persistence?
- Is the server only active in Configure mode and fully torn down otherwise?

Provide specific, actionable feedback for improvements. Where an issue relates to a spec requirement, cite the relevant section of `Specification.md`.
