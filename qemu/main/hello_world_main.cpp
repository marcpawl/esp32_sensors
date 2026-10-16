/*
 * SPDX-FileCopyrightText: 2010-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hal/led.hpp"
#include "hal/onewire.hpp"
#include "hal/switch.hpp"
#include "sdkconfig.h"

namespace {

// Exercises the shared HAL switch interface via its QEMU stub (`hal_stub`).
// The stub's pin states are settable in software, so this proves the interface
// is linked and reports the raw pin states — including the bad-hardware cases
// (both poles on, both poles off) the real 3-position switch cannot produce.
void report_switch() {
    static_assert(thermo::hal::SwitchLike<thermo::hal::Switch>);

    thermo::hal::Switch sw = thermo::hal::Switch::create(/*first=*/1,
                                                         /*third=*/2);
    if (sw.init() != ESP_OK) {
        std::printf("Switch init failed\n");
        return;
    }

    const thermo::hal::Pins first{thermo::hal::PinState::kOn,
                                  thermo::hal::PinState::kOff};
    const thermo::hal::Pins center{thermo::hal::PinState::kOff,
                                   thermo::hal::PinState::kOff};
    const thermo::hal::Pins third{thermo::hal::PinState::kOff,
                                  thermo::hal::PinState::kOn};

    const struct {
        const char* name;
        thermo::hal::Pins pins;
    } cases[] = {
        {"First (Configure)", first},
        {"Center (Off)", center},
        {"Third (Run)", third},
        {"Fault (both on)",
         {thermo::hal::PinState::kOn, thermo::hal::PinState::kOn}},
    };

    for (auto const& c : cases) {
        sw.set_pins(c.pins);
        const thermo::hal::Pins got = sw.read();
        std::printf("Switch %-18s -> first=%d third=%d\n", c.name,
                    got.first_on() ? 1 : 0, got.third_on() ? 1 : 0);
    }
}

// Exercises the shared HAL LED interface via its QEMU stub (`hal_stub`). The
// stub logs every write as "<name>: On" / "<name>: Off", so this proves the
// interface is linked and that named LEDs drive the expected log lines.
void report_led() {
    static_assert(thermo::hal::LedLike<thermo::hal::Led>);

    thermo::hal::Led led =
        thermo::hal::Led::create(/*gpio=*/17, /*name=*/"Green");
    if (led.init() != ESP_OK) {
        std::printf("Led init failed\n");
        return;
    }

    led.write(thermo::hal::LedState::kOn);
    led.write(thermo::hal::LedState::kOff);
}

// Prints a DS18B20 reading as "<rom hex> <temp>" when the temperature is
// present, or "<rom hex> --" when it is missing (nullopt).
void print_reading(const thermo::hal::DS18B20Reading& reading) {
    const thermo::hal::RomId& rom = reading.rom;
    std::printf("OneWire sensor %02X%02X%02X%02X%02X%02X%02X%02X ",
                rom.bytes[0], rom.bytes[1], rom.bytes[2], rom.bytes[3],
                rom.bytes[4], rom.bytes[5], rom.bytes[6], rom.bytes[7]);
    if (reading.temp_c_x100.has_value()) {
        std::printf("%d.%02d C\n", reading.temp_c_x100.value() / 100,
                    std::abs(reading.temp_c_x100.value() % 100));
    } else {
        std::printf("--\n");
    }
}

// Exercises the shared HAL 1-Wire bus interface via its QEMU stub (`hal_stub`).
// The stub's sensors are settable in software, so this proves the interface is
// linked and reports the DS18B20 ROM IDs and readings the caller configured.
void report_onewire() {
    static_assert(thermo::hal::OneWireLike<thermo::hal::OneWire>);

    thermo::hal::OneWire bus = thermo::hal::OneWire::create(/*gpio=*/4);
    if (bus.init() != ESP_OK) {
        std::printf("OneWire init failed\n");
        return;
    }

    // Report the default collection, then override it to prove set_sensors().
    std::printf("OneWire discovered %u default sensor(s)\n",
                static_cast<unsigned>(bus.discover_sensors().size()));

    const std::vector<thermo::hal::DS18B20Reading> readings = {
        {{0x28, 0xFF, 0x64, 0xA1, 0xB2, 0x16, 0x03, 0xC4}, 2150},
        {{0x28, 0xFF, 0x3B, 0x2C, 0x77, 0x16, 0x04, 0x2F}, -750},
    };
    bus.set_sensors(readings);

    const std::vector<thermo::hal::RomId> found = bus.discover_sensors();
    std::printf("OneWire discovered %u sensor(s)\n",
                static_cast<unsigned>(found.size()));
    for (const thermo::hal::RomId& rom : found) {
        std::printf("OneWire sensor %02X%02X%02X%02X%02X%02X%02X%02X\n",
                    rom.bytes[0], rom.bytes[1], rom.bytes[2], rom.bytes[3],
                    rom.bytes[4], rom.bytes[5], rom.bytes[6], rom.bytes[7]);
    }

    for (const thermo::hal::DS18B20Reading& reading : bus.read()) {
        print_reading(reading);
    }
}

}  // namespace

extern "C" void app_main(void) {
    std::printf("Hello world!\n");

    report_switch();
    report_led();
    report_onewire();

    /* Print chip information */
    esp_chip_info_t chip_info;
    uint32_t flash_size;
    esp_chip_info(&chip_info);
    std::printf("This is %s chip with %d CPU core(s), %s%s%s%s%s%s, ",
                CONFIG_IDF_TARGET, chip_info.cores,
                (chip_info.features & CHIP_FEATURE_WIFI_BGN) ? "WiFi" : "",
                ((chip_info.features & CHIP_FEATURE_WIFI_BGN) &&
                 (chip_info.features & (CHIP_FEATURE_BT | CHIP_FEATURE_BLE)))
                    ? "/"
                    : "",
                (chip_info.features & CHIP_FEATURE_BT) ? "BT" : "",
                ((chip_info.features & CHIP_FEATURE_BT) &&
                 (chip_info.features & CHIP_FEATURE_BLE))
                    ? "/"
                    : "",
                (chip_info.features & CHIP_FEATURE_BLE) ? "BLE" : "",
                (chip_info.features & CHIP_FEATURE_IEEE802154)
                    ? ", 802.15.4 (Zigbee/Thread)"
                    : "");

    unsigned major_rev = chip_info.revision / 100;
    unsigned minor_rev = chip_info.revision % 100;
    std::printf("silicon revision v%d.%d, ", major_rev, minor_rev);
    if (esp_flash_get_size(NULL, &flash_size) != ESP_OK) {
        std::printf("Get flash size failed");
        return;
    }

    std::printf("%" PRIu32 "MB %s flash\n",
                flash_size / (uint32_t)(1024 * 1024),
                (chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "embedded"
                                                              : "external");

    std::printf("Minimum free heap size: %" PRIu32 " bytes\n",
                esp_get_minimum_free_heap_size());

    for (int i = 10; i >= 0; i--) {
        std::printf("Restarting in %d seconds...\n", i);
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
    std::printf("Restarting now.\n");
    fflush(stdout);
    esp_restart();
}
