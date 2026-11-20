# ESP-IDF requires the component that defines `app_main` to be named `main`.

idf_component_register(
    SRCS
        "main.cpp"
        "console_task.cpp"
    INCLUDE_DIRS
        "."
    REQUIRES
        hal_stub
        libespidfcpp
        liblogic
        libsimulator
        json
)

# Headers are target sources so a CMake file-API client (CLion) attaches them
# to this target; CMake adds no compile step for a header.
target_sources(${COMPONENT_LIB} PRIVATE "console_task.hpp")
