# `libsimulator` — host-side simulator support.
#
# This component is intentionally empty. It exists so the simulator tree has a
# component of its own that the simulator executable (`exe/donny`) and its
# tests (`tests/test-simulator`) can require, and so new simulator code has one
# obvious home. Add `.cpp` files under `SRCS` (and public headers under
# `INCLUDE_DIRS`) below; consumers already name the component, so nothing else
# needs to change.
#
# It targets ESP-IDF's `linux` target only: the simulator emulates hardware, so
# it must never be built into the firmware for the chip. `mcgowan` does not put
# this grouping directory on its component search path, and the guard below
# turns an accidental link into a configure-time error instead of a surprise
# later.
if(NOT IDF_TARGET STREQUAL "linux")
    message(FATAL_ERROR
        "libsimulator targets the ESP-IDF 'linux' target only "
        "(IDF_TARGET is '${IDF_TARGET}').\n"
        "It must never be required by a chip-target project.")
endif()

idf_component_register()
