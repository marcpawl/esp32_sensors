# The shared `esp_timer` test cases, linked by both the on-target and the
# host-stub executables. `WHOLE_ARCHIVE` keeps the Unity registrations in the
# final ELF even though nothing references them.
#
# `esp_timer` supplies the API under test. `esp_hw_support` supplies
# `esp_etm.h`, which `esp_timer.h` includes; the linux port of `esp_hw_support`
# is header-only for that purpose.

idf_component_register(
    SRCS
        "src/test_esp_timer.cpp"
    PRIV_REQUIRES
        unity
        esp_timer
        esp_hw_support
        freertos
    WHOLE_ARCHIVE
)
