# The lowest layer: the fixed-capacity containers. They carry no ESP-IDF
# dependency, so every other component may use them on both the chip and the
# host.
#
# `hal_common` requires them because the shared 1-Wire vocabulary names
# `static_vector`, and `libespidfcpp` requires them for its string type and
# HTTP vocabulary. `liblogic` names `static_vector` in `logic/rtc_state.hpp`.

idf_component_register(
    INCLUDE_DIRS
        "include"
)

# Headers are target sources so a CMake file-API client (CLion) attaches them
# to this target; CMake adds no compile step for a header.
target_sources(${COMPONENT_LIB} PRIVATE
    "include/utility/isr_safe.hpp"
    "include/utility/sequence_lock.hpp"
    "include/utility/static_string.hpp"
    "include/utility/static_vector.hpp"
)
