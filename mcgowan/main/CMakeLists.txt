# ESP-IDF requires the component that defines `app_main` to be named `main`,
# so the application sources live under `mcgowan/src/` and are compiled here.
#
# **Add new sources here** or they will not compile. Board-independent logic
# lives in the `logic` component instead.

set(mcgowan_src "${CMAKE_CURRENT_LIST_DIR}/../src")

idf_component_register(
    SRCS
        "${mcgowan_src}/main.cpp"
        "${mcgowan_src}/app_config.cpp"
        "${mcgowan_src}/app_config_store.cpp"
        "${mcgowan_src}/battery_monitor.cpp"
        "${mcgowan_src}/power_manager.cpp"
        "${mcgowan_src}/power_source.cpp"
        "${mcgowan_src}/onewire_bus.cpp"
        "${mcgowan_src}/ds18b20.cpp"
        "${mcgowan_src}/sensor_manager.cpp"
        "${mcgowan_src}/rtc_storage.cpp"
        "${mcgowan_src}/configure_mode.cpp"
        "${mcgowan_src}/https_server.cpp"
        "${mcgowan_src}/config_page.cpp"
        "${mcgowan_src}/wifi_ap.cpp"
        "${mcgowan_src}/ntp_client.cpp"
        "${mcgowan_src}/run_mode.cpp"
    INCLUDE_DIRS
        "${mcgowan_src}/include"
    REQUIRES
        hal_board
        logic
        espidf_cpp
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
