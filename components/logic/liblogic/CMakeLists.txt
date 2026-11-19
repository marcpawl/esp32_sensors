# This component must NOT require `hal_board` or `hal_stub`: the logic
# templates over `hal::BoardLike`, and each project links exactly one of those
# two implementations (see the `EXCLUDE_COMPONENTS` lines in the subproject
# CMakeLists.txt files). Requiring either here would drag the other into every
# project.
#
# FreeRTOS, logging, and `esp_common` come from ESP-IDF's common requirements
# (the same reason `libespidfcpp` lists no dependencies).
#
# Add new .cpp files under SRCS below; consumers already `REQUIRES
# liblogic`, so
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
        libespidfcpp
        # `logic/rtc_state.hpp` names `utility::static_vector` in its public
        # members, so the plain-capacity container is a public requirement too.
        libutility
        hal_common
        # boost-ext/sml (v1.2.0), via the wrapper component in
        # `components/sml` that exposes the `third_party/sml` submodule. The
        # state machines live in this component, so its consumers do not name
        # `sml` themselves.
        sml
)

# Headers are target sources so a CMake file-API client (CLion) attaches them
# to this target; CMake adds no compile step for a header.
target_sources(${COMPONENT_LIB} PRIVATE
    "include/logic/application.hpp"
    "include/logic/led_manager.hpp"
    "include/logic/led_task.hpp"
    "include/logic/limits.hpp"
    "include/logic/mode.hpp"
    "include/logic/mode_selection.hpp"
    "include/logic/payload.hpp"
    "include/logic/reboot.hpp"
    "include/logic/rom_id.hpp"
    "include/logic/rtc_state.hpp"
    "include/logic/self_signed_cert.hpp"
    "include/logic/temp_format.hpp"
)
