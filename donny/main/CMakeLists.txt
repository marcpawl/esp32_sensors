# Application component for the `donny` linux-emulated project.
#
# `idf.py` requires the component that defines `app_main` to be named `main`,
# so the single source lives in `donny/main/`.
#
# The project links the reusable components shared from the repo-root
# `components/` directory (see ../CMakeLists.txt): the pin-free HAL
# (`hal_stub`), the C++ ESP-IDF wrappers (`espidf_cpp`), and the
# board-independent application logic (`logic`). `hal_board` is excluded by the
# parent CMakeLists so `hal_stub` is the only HAL implementation in view.
#
# `json` is the vendored nlohmann/json component under the repo-root
# `third_party/` (see that component's CMakeLists for why vendored code lives
# outside `components/`).

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
