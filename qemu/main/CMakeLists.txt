# QEMU smoke-test component.
#
# The project's `main` component (`app_main`). This project is standalone and
# intentionally does not compile the production `mcgowan/src/` sources. The source is
# the ESP-IDF `hello_world` example, used to verify the QEMU build/boot workflow
# (`idf.py qemu` -> `qemu-system-xtensa -M esp32s3`) on the ESP32-S3 target.
#
# `spi_flash` is a private requirement of the hello_world source, which reads
# the flash chip size via esp_flash_get_size(). `hal_stub` provides the
# pin-free board/LED implementations shared with the firmware via the
# `hal/*.hpp` headers.
#
# `logic` is required (not just private) because this project runs the same
# board-independent application logic as the firmware — starting with the LED
# task in `logic/led_task.hpp` — against the stub board, so QEMU exercises real
# code instead of a copy. `logic` itself requires `hal_common` (the HAL
# vocabulary) but never a concrete HAL implementation; `hal_stub` supplies the
# board this project instantiates the logic with.

idf_component_register(
    SRCS
        "hello_world_main.cpp"
    REQUIRES
        logic
    PRIV_REQUIRES
        spi_flash
        hal_stub
    INCLUDE_DIRS
        "."
)
