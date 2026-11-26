# `test-espidf-cpp-lib` holds every Unity registration, including the
# InterruptHandle cases. This `main` only runs the whole suite. `unity`
# provides the runner.

idf_component_register(
    SRCS
        "main.cpp"
    REQUIRES
        test-espidf-cpp-lib
        unity
    WHOLE_ARCHIVE
)
