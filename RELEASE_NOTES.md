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

Not yet implemented
-------------------

- Run mode network path: Wi-Fi station connect with cached fast reconnect,
  batch upload of the payload, and exponential backoff.
