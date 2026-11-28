# The component under test is `libsimulator`; these cases exercise its
# NVS-backed switch restore. `WHOLE_ARCHIVE` keeps their `TEST_CASE`
# registrations linked into the final executable even though nothing
# references them directly. `hal_stub` supplies the GPIO mock, `libespidfcpp`
# the NVS and switch wrappers, and `liblogic` the shared pin map.

idf_component_register(
    SRCS
        "test_main.cpp"
        "test_restore_hardware.cpp"
    INCLUDE_DIRS
        "."
    REQUIRES
        libsimulator
    PRIV_REQUIRES
        libespidfcpp
        liblogic
        hal_stub
        unity
    WHOLE_ARCHIVE
)
