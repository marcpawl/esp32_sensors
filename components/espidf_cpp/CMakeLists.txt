# espidf_cpp: reusable C++ wrappers over ESP-IDF / FreeRTOS C APIs.
#
# ESP-IDF's C API is easy to misuse: functions return an `esp_err_t` that is
# routinely ignored, handles must be released by a separate call, and results
# come back through out-parameters. This component collects thin C++ wrappers
# that make those contracts explicit and hard to break:
#
#   espidf_cpp/one_message_queue.hpp   OneMessageQueue<T> — a single-slot,
#                                      typed, RAII-managed FreeRTOS queue.
#
# The wrappers add typing and ownership, not policy; they stay close enough to
# the C API that their behaviour is predictable.
#
# Every header here is host-testable and exercised by the `espidf-cpp-test`
# subproject (ESP-IDF `linux` target). Keep new wrappers similarly free of
# chip-only APIs, or guard the chip-only parts, so that stays true.
#
# FreeRTOS and logging come from ESP-IDF's common requirements, so no component
# dependency is listed.

idf_component_register(
    INCLUDE_DIRS
        "include"
)
