# logic: hardware-independent application logic.
#
# Holds behaviour and vocabulary that is not tied to a specific board: the LED
# status task (`logic/led_task.hpp`), the transmission gate
# (`logic/led_manager.hpp`), the fixed capacities (`logic/limits.hpp`), the
# temperature serialization and its "no reading" sentinel
# (`logic/temp_format.hpp`), the RTC state's shape and buffer operations
# (`logic/rtc_state.hpp`, `src/rtc_buffer.cpp` — the `RTC_DATA_ATTR` storage
# itself is firmware-only, in `mcgowan/src/rtc_storage.cpp`), the operating
# modes and their mapping from the mode-sense poles (`logic/mode.hpp`,
# `logic/mode_selection.hpp`), the Configure-mode self-signed TLS certificate
# (`logic/self_signed_cert.hpp`, `src/self_signed_cert.cpp`) and, as it grows,
# the rest of the logic shared by the firmware and the host tests. It
# depends only on `espidf_cpp` and the HAL *vocabulary* (`hal_common`), so the
# same code links into:
#
#   * `mcgowan/` — production firmware, driving the GPIO-backed `hal::Board`
#                  from `hal_board`.
#   * `test/`    — host unit tests (`linux` target), which instantiate the
#                  templated logic against the pin-free stub board.
#
# This component must NOT require `hal_board` or `hal_stub`: the logic
# templates over `hal::BoardLike`, and each project links exactly one of those
# two implementations (see the `EXCLUDE_COMPONENTS` lines in the subproject
# CMakeLists.txt files). Requiring either here would drag the other into every
# project.
#
# FreeRTOS, logging, and `esp_common` come from ESP-IDF's common requirements
# (the same reason `espidf_cpp` lists no dependencies).
#
# Add new .cpp files under SRCS below; consumers already `REQUIRES logic`, so
# nothing else needs to change.

idf_component_register(
    SRCS
        "src/temp_format.cpp"
        "src/rtc_buffer.cpp"
        "src/self_signed_cert.cpp"
    INCLUDE_DIRS
        "include"
    REQUIRES
        espidf_cpp
        hal_common
)
