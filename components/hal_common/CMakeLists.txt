# hal_common: shared, implementation-free HAL vocabulary.
#
# Header-only component defining the types common to every HAL device
# implementation (starting with the switch's `PinState`, `Pins`, and the
# `SwitchLike` concept in `hal/switch_types.hpp`). It carries no hardware
# dependency, so both `hal_board` and `hal_stub` can require it and the shared
# definitions live in exactly one place.
#
# This component is NOT mutually exclusive with anything: it is designed to be
# shared. Only the `hal_board` / `hal_stub` pair is exclusive.

idf_component_register(
    INCLUDE_DIRS
        "include"
)
