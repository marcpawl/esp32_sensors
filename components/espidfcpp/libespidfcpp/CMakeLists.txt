# FreeRTOS and logging come from ESP-IDF's common requirements. `esp_http_server`
# is a public requirement because `http_server.hpp` includes `esp_http_server.h`;
# it is also available for the `linux` target.
#
# `esp_timer` is public because `timer.hpp` includes `esp_timer.h`, and
# `esp_hw_support` supplies `esp_etm.h`, which `esp_timer.h` includes on the
# `linux` target as well.
#
# `libutility` is public because `task.hpp`, `timer.hpp` and the HTTP
# vocabulary use `static_string` / `static_vector` from it.
#
# `esp_ringbuf` is public because `handle.hpp` and `ring_buffer.hpp` include
# `freertos/ringbuf.h`; the component is available for the `linux` target too.
#
# `hal_common` is public because `interrupt_handle.hpp` names `hal::Gpio`.
#
# `gpio.hpp` includes the GPIO driver header. On the chip it needs
# `esp_driver_gpio`; on `linux` that component is empty, so the mock header
# and definitions come from `hal_stub`.

if(IDF_TARGET STREQUAL "linux")
    set(GPIO_DEPENDENCY hal_stub)
else()
    set(GPIO_DEPENDENCY esp_driver_gpio)
endif()

idf_component_register(
    SRCS
        "src/http_server.cpp"
        "src/interrupt_handle.cpp"
        "src/mutex.cpp"
        "src/switch.cpp"
        "src/task.cpp"
    INCLUDE_DIRS
        "include"
    REQUIRES
        libutility
        hal_common
        esp_http_server
        esp_ringbuf
        esp_timer
        esp_hw_support
	nvs_flash
        ${GPIO_DEPENDENCY}
)

# Headers are target sources so a CMake file-API client (CLion) attaches them
# to this target; CMake adds no compile step for a header.
target_sources(${COMPONENT_LIB} PRIVATE
    "include/espidf_cpp/gpio.hpp"
    "include/espidf_cpp/handle.hpp"
    "include/espidf_cpp/http_server.hpp"
    "include/espidf_cpp/http_server_types.hpp"
    "include/espidf_cpp/interrupt_handle.hpp"
    "include/espidf_cpp/mutex.hpp"
    "include/espidf_cpp/nvs.hpp"
    "include/espidf_cpp/one_message_queue.hpp"
    "include/espidf_cpp/ring_buffer.hpp"
    "include/espidf_cpp/softswitch.hpp"
    "include/espidf_cpp/switch.hpp"
    "include/espidf_cpp/task.hpp"
    "include/espidf_cpp/timer.hpp"
)
