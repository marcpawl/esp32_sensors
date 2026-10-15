# QEMU smoke-test component.
#
# The project's `main` component (`app_main`). This project is standalone and
# intentionally does not compile the production `main/` sources. The source is
# the ESP-IDF `hello_world` example, used to verify the QEMU build/boot workflow
# (`idf.py qemu` -> `qemu-system-xtensa -M esp32s3`) on the ESP32-S3 target.
#
# `spi_flash` is a private requirement of the hello_world source, which reads
# the flash chip size via esp_flash_get_size(). `hal_stub` provides the
# pin-free switch implementation shared with the firmware via `hal/switch.hpp`.

idf_component_register(
    SRCS
        "hello_world_main.cpp"
    PRIV_REQUIRES
        spi_flash
        hal_stub
    INCLUDE_DIRS
        "."
)
