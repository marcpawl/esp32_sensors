# hal_stub: stub hardware abstractions for QEMU and host tests.
#
# Exposes the same `hal/switch.hpp` and `hal/led.hpp` as `hal_board` but with
# deterministic, side-effect-free implementations that need no real hardware.
# The QEMU project (`qemu/`) and host tests depend on this component instead of
# `hal_board`; a project must depend on exactly one of the two.
#
# Like `hal_board`, it depends on the header-only `hal_common` component for the
# shared device vocabulary (`PinState`, `Pins`, `SwitchLike`, `LedState`,
# `LedLike`).

idf_component_register(
    SRCS
        "switch_stub.cpp"
        "led_stub.cpp"
    INCLUDE_DIRS
        "include"
    REQUIRES
        hal_common
)
