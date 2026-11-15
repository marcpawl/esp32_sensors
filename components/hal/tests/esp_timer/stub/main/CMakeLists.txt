# `esp_timer_tests` holds every Unity registration (linked WHOLE_ARCHIVE), so
# this `main` only initializes the library and runs the suite. `hal_stub`
# provides the esp_timer implementation on `linux`; `esp_timer` provides the
# headers and `unity` the runner.

idf_component_register(
    SRCS
        "main.cpp"
    INCLUDE_DIRS
        "."
    REQUIRES
        esp_timer_tests
        hal_stub
        esp_timer
        unity
)
