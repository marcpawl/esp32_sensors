# `esp_timer_tests` holds every Unity registration (linked WHOLE_ARCHIVE), so
# this `main` only initializes the library and runs the suite. `esp_timer`
# provides the API under test; `unity` provides the runner.

idf_component_register(
    SRCS
        "main.cpp"
    INCLUDE_DIRS
        "."
    REQUIRES
        esp_timer_tests
        esp_timer
        unity
)
