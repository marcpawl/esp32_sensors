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
