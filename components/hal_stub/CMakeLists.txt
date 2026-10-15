# hal_stub: stub hardware abstractions for QEMU and host tests.
#
# Exposes the same headers as `hal_board` (starting with `hal/switch.hpp`) but
# with deterministic, side-effect-free implementations that need no real
# hardware. The QEMU project (`qemu/`) and host tests depend on this component
# instead of `hal_board`; a project must depend on exactly one of the two.

idf_component_register(
    SRCS
        "switch_stub.cpp"
    INCLUDE_DIRS
        "include"
)
