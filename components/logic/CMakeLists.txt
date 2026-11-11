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
# (`logic/self_signed_cert.hpp`, `src/self_signed_cert.cpp`), the ROM-ID hex
# conversions (`logic/rom_id.hpp`, `src/rom_id.cpp`), the §9 upload payload
# (`logic/payload.hpp`, `src/payload.cpp`), the reboot helper
# (`logic/reboot.hpp`, `src/reboot.cpp`) and, as it grows, the rest of the
# logic shared by the firmware and the host tests. It
# depends on `espidf_cpp`, the HAL *vocabulary* (`hal_common`), and `sml` — the
# thin wrapper component around the `third_party/sml` git submodule, which
# provides boost-ext/sml for the state machines this component owns. The same
# code links into:
#
#   * `mcgowan/` — production firmware, driving the GPIO-backed `hal::Board`
#                  from `hal_board`.
#   * `test-logic/` — host unit tests (`linux` target), which instantiate the
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
        "src/rom_id.cpp"
        "src/payload.cpp"
        "src/reboot.cpp"
    INCLUDE_DIRS
        "include"
    REQUIRES
        espidf_cpp
        hal_common
        # boost-ext/sml (v1.2.0), via the wrapper component in
        # `components/sml` that exposes the `third_party/sml` submodule. The
        # state machines live in this component, so its consumers do not name
        # `sml` themselves.
        sml
)
