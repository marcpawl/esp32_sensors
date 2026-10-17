# Application component for the `mcgowan` firmware.
#
# `idf.py` requires the component that defines `app_main` to be named `main`,
# so two names are in play:
#
#   * `main/`       — this ESP-IDF component directory (fixed by ESP-IDF).
#   * `mcgowan/src/` — the C++ sources and headers it compiles, so the
#                      application code is clearly owned by the mcgowan project.
#
# The reusable HAL components are shared from the repo-root `components/`
# directory (see ../CMakeLists.txt). **Add new sources here** or they will not
# compile.

set(mcgowan_src "${CMAKE_CURRENT_LIST_DIR}/../src")

idf_component_register(
    SRCS
        "${mcgowan_src}/main.cpp"
        "${mcgowan_src}/app_config.cpp"
        "${mcgowan_src}/app_config_store.cpp"
        "${mcgowan_src}/battery_monitor.cpp"
        "${mcgowan_src}/power_manager.cpp"
        "${mcgowan_src}/power_source.cpp"
        "${mcgowan_src}/led_task.cpp"
        "${mcgowan_src}/onewire_bus.cpp"
        "${mcgowan_src}/rom_id.cpp"
        "${mcgowan_src}/ds18b20.cpp"
        "${mcgowan_src}/sensor_manager.cpp"
        "${mcgowan_src}/rtc_state.cpp"
        "${mcgowan_src}/rtc_buffer.cpp"
        "${mcgowan_src}/temp_format.cpp"
        "${mcgowan_src}/payload.cpp"
        "${mcgowan_src}/configure_mode.cpp"
        "${mcgowan_src}/https_server.cpp"
        "${mcgowan_src}/config_page.cpp"
        "${mcgowan_src}/wifi_ap.cpp"
        "${mcgowan_src}/ntp_client.cpp"
        "${mcgowan_src}/run_mode.cpp"
        "${mcgowan_src}/reboot.cpp"
        "${mcgowan_src}/certs/self_signed_cert.cpp"
    INCLUDE_DIRS
        "${mcgowan_src}/include"
    REQUIRES
        hal_board
        nvs_flash
        esp_wifi
        esp_netif
        esp_event
        esp_https_server
        esp_http_client
        esp_adc
        esp_driver_gpio
        esp_driver_usb_serial_jtag
        esp_driver_uart
        esp_timer
        mbedtls
        lwip
)
