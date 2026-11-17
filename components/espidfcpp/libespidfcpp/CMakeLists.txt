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

idf_component_register(
    SRCS
        "src/http_server.cpp"
    INCLUDE_DIRS
        "include"
    REQUIRES
        libutility
        esp_http_server
        esp_timer
        esp_hw_support
)
