// Power-source detection (§2.2, Rev 13).
//
// Rev 13 of the specification notes that the solar battery may be absent and
// that the device may instead be powered over USB — either from an external
// battery pack or from a connected computer. PowerManager identifies which of
// these three sources is supplying the board at construction time.
#pragma once

#include <cstdint>
#include <optional>

#include "app_config.hpp"
#include "battery_monitor.hpp"

namespace thermo {

// Where the ESP32 is currently drawing power from (§2.2).
enum class PowerSource : std::uint8_t {
    kSolarBattery = 0,  // Solar-powered battery pack is installed and powering.
    kUsbBatteryPack = 1,  // USB port, dumb battery pack (VBUS only, no host).
    kUsbComputer = 2,     // USB port, host computer (enumerated data lines).
};

// Human-readable name for a power source, e.g. for logging. The returned
// pointer is to a string literal and has static storage duration.
const char* to_string(PowerSource source) noexcept;

// Detects the power source at construction time and, when the board runs from
// the solar battery, owns the BatteryMonitor used to read and evaluate it.
//
// Detection strategy:
//   * USB host enumerated on the USB-Serial-JTAG data lines -> kUsbComputer.
//   * Otherwise USB VBUS present but no host               -> kUsbBatteryPack.
//   * Otherwise the solar battery divider reads a
//     plausible voltage                                   -> kSolarBattery.
//
// A connected computer sends SOF packets, so `usb_serial_jtag_is_connected()`
// is true only for a host; a dumb battery pack provides VBUS but no SOF, so it
// never enumerates. The solar battery may be absent entirely; in that case the
// source is one of the USB variants and `battery()` is empty.
class PowerManager {
  public:
    explicit PowerManager(Config const& config);

    // The source detected at construction; stable for the object's lifetime.
    PowerSource source() const noexcept { return source_; }

    // Present only when the solar battery is installed and detected, i.e.
    // source() == PowerSource::kSolarBattery.
    const std::optional<BatteryMonitor>& battery() const noexcept {
        return battery_;
    }

    // True when running from a USB port (either battery pack or computer).
    bool on_usb() const noexcept {
        return source_ != PowerSource::kSolarBattery;
    }

  private:
    // Raw detection, split out so the constructor reads as a decision tree.
    PowerSource detect() const noexcept;

    // USB host present on the data lines (a connected computer).
    static bool usb_host_connected() noexcept;

    // USB VBUS present regardless of whether a host is attached.
    static bool usb_vbus_present() noexcept;

    // True when the battery divider reads a plausible voltage, i.e. the solar
    // battery is actually installed and supplying the board.
    bool solar_battery_installed() const noexcept;

    Config const& config_;
    PowerSource source_;
    std::optional<BatteryMonitor> battery_;
};

}  // namespace thermo
