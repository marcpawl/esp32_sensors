# `libsimulator` — host-side simulator support.
#
# It targets ESP-IDF's `linux` target only: the simulator emulates hardware, so
# it must never be built into the firmware for the chip. `mcgowan` does not put
# this grouping directory on its component search path, and the guard below
# turns an accidental link into a configure-time error instead of a surprise
# later.
#
# `liblogic` is public because `simulator/board_simulator.hpp` derives from
# `logic::Board`. `libespidfcpp` stays private: only the source uses
# `SoftSwitch` and `Nvs` directly.
if(NOT IDF_TARGET STREQUAL "linux")
    message(FATAL_ERROR
        "libsimulator targets the ESP-IDF 'linux' target only "
        "(IDF_TARGET is '${IDF_TARGET}').\n"
        "It must never be required by a chip-target project.")
endif()

idf_component_register(
    SRCS
        "src/restore_hardware.cpp"
    INCLUDE_DIRS
        "include"
    REQUIRES
        liblogic
    PRIV_REQUIRES
        libespidfcpp
)

# Headers are target sources so a CMake file-API client (CLion) attaches them
# to this target; CMake adds no compile step for a header.
target_sources(${COMPONENT_LIB} PRIVATE
    "include/simulator/board_simulator.hpp"
    "include/simulator/restore_hardware.hpp"
)
