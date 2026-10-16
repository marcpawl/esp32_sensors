# espidf_cpp: small C++ wrappers around ESP-IDF / FreeRTOS C APIs.
#
# Header-only component collecting the thin, RAII-flavoured C++ shims used
# across the firmware:
#
#   * `handle_deleter.hpp`       - `std::unique_ptr` deleters for FreeRTOS
#                                  handles (`UniqueTaskHandle`,
#                                  `UniqueQueueHandle`).
#   * `task.hpp`                 - `thermo::Task<Runner>`: run a `Runner::run()`
#                                  body on a FreeRTOS task.
#   * `one_message_queue.hpp`    - `OneMessageQueue<T>`: a single-slot queue
#                                  whose send() overwrites any unread message.
#   * `thermo_static_vector.hpp` - `thermo::static_vector<T, N>`, the
#                                  fixed-capacity sequence container that
#                                  stands in for `std::static_vector`.
#   * `thermo_static_string.hpp` - `thermo::static_string<N>`, a
#                                  fixed-capacity, always-NUL-terminated string
#                                  built on `static_vector` (used by `task.hpp`
#                                  for the copied task name).
#
# The first three wrap ESP-IDF/FreeRTOS rather than the hardware, so they are
# independent of the HAL (`hal_common` / `hal_board` / `hal_stub`) and do not
# reference it. The two static containers carry no ESP-IDF dependency at all
# and live here so `task.hpp` can use `static_string` without this component
# depending on `main`.
#
# Like the HAL headers, this component is header-only; callers compile the
# implementations inline. Requiring the underlying IDF components keeps the
# FreeRTOS and `esp_log` symbols visible to whoever includes the headers.

idf_component_register(
    INCLUDE_DIRS
        "include"
    REQUIRES
        freertos
        esp_common
        log
)
