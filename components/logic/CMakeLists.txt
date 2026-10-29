# logic: hardware-independent application logic.
#
# Holds behaviour and vocabulary that is not tied to a specific board: the LED
# status task (`logic/led_task.hpp`), the transmission gate
# (`logic/led_manager.hpp`), the fixed capacities (`logic/limits.hpp`), the
# operating modes and their mapping from the mode-sense poles
# (`logic/mode.hpp`, `logic/mode_selection.hpp`) and, as it grows, the rest of
# the logic shared by the firmware and the host tests. It depends only on
# `espidf_cpp` and the HAL *vocabulary* (`hal_common`), so the same code links
# into:
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
# Header-only today. When .cpp files are added, list them under SRCS below;
# consumers already `REQUIRES logic`, so nothing else needs to change.

idf_component_register(
    SRCS
        # (none yet — headers only; add new .cpp files here)
    INCLUDE_DIRS
        "include"
    REQUIRES
        espidf_cpp
        hal_common
)
