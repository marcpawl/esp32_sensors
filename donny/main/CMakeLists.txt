# ESP-IDF requires the component that defines `app_main` to be named `main`.

idf_component_register(
    SRCS
        "main.cpp"
        "console_task.cpp"
    INCLUDE_DIRS
        "."
    REQUIRES
        hal_stub
        espidf_cpp
        logic
        json
)
