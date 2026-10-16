# Native (linux-target) Unity tests.
#
# The project's `main` component (`app_main`). This project is standalone and
# intentionally does not compile the production `main/` sources. It runs the
# Unity suite on ESP-IDF's `linux` target (real FreeRTOS, native ELF), so it
# covers code that the plain host suite does not drive as hard: the
# FreeRTOS-backed `thermo::Task` (`test_task.cpp`).
#
# The HAL cases in `test_switch.cpp`, `test_led.cpp`, `test_push_button.cpp`,
# `test_onewire.cpp`, `test_chip_temperature.cpp`, and `test_board.cpp` are
# **linkage checks**: they construct the pin-free `hal_stub` devices to prove
# the stub HAL links and works natively. Deeper device behaviour is covered by
# the host suite (`test/`).
#
# `unity` provides the test runner; `espidf_cpp` provides `Task` and the
# `HttpsServer` router; `hal_stub` provides the stubbed HAL devices.
# `esp_https_server` (pulled in by `espidf_cpp`) gives the `HttpsServer` tests a
# real plain-HTTP listener on the linux target (which backs it with the host's
# POSIX sockets); `esp_event` provides the default event loop that listener
# posts its lifecycle events to. WHOLE_ARCHIVE is required so the TEST_CASE
# registrations link into the executable even though nothing references them.
idf_component_register(
    SRCS
        "test_main.cpp"
        "test_task.cpp"
        "test_switch.cpp"
        "test_led.cpp"
        "test_push_button.cpp"
        "test_onewire.cpp"
        "test_chip_temperature.cpp"
        "test_board.cpp"
        "test_https_server.cpp"
    PRIV_REQUIRES
        unity
        espidf_cpp
        hal_stub
        esp_https_server
        esp_event
    WHOLE_ARCHIVE
)
