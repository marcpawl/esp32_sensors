# espidf_cpp: reusable C++ wrappers over ESP-IDF / FreeRTOS C APIs.
#
# ESP-IDF's C API is easy to misuse: functions return an `esp_err_t` that is
# routinely ignored, handles must be released by a separate call, and results
# come back through out-parameters. This component collects thin C++ wrappers
# that make those contracts explicit and hard to break:
#
#   espidf_cpp/one_message_queue.hpp   OneMessageQueue<T> — a single-slot,
#                                      typed, RAII-managed FreeRTOS queue.
#   espidf_cpp/task.hpp                Task<Runner> — a typed task created
#                                      from a `[[noreturn]] run()` body, with
#                                      TaskParams and an owned handle.
#   espidf_cpp/handle.hpp              HandleDeleter plus UniqueTaskHandle and
#                                      UniqueQueueHandle (RAII FreeRTOS
#                                      handles).
#   espidf_cpp/static_vector.hpp       A fixed-capacity, inline-storage vector
#                                      (a subset of std::static_vector).
#   espidf_cpp/static_string.hpp       A fixed-capacity, NUL-terminated string
#                                      built on static_vector.
#
# The wrappers add typing and ownership, not policy; they stay close enough to
# the C API that their behaviour is predictable.
#
# The wrappers are host-testable: `espidf-cpp-test` (ESP-IDF `linux` target)
# exercises one_message_queue and static_vector/static_string. `task.hpp` needs
# a real FreeRTOS scheduler, so it is verified on target and by the firmware
# build. Keep new wrappers similarly free of chip-only APIs, or guard the
# chip-only parts, so host tests stay possible.
#
# FreeRTOS and logging come from ESP-IDF's common requirements, so no component
# dependency is listed.

idf_component_register(
    INCLUDE_DIRS
        "include"
)
