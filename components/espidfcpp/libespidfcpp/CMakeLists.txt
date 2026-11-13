# FreeRTOS and logging come from ESP-IDF's common requirements. `esp_http_server`
# is a public requirement because `http_server.hpp` includes `esp_http_server.h`;
# it is also available for the `linux` target.

idf_component_register(
    SRCS
        "src/http_server.cpp"
    INCLUDE_DIRS
        "include"
    REQUIRES
        esp_http_server
)
