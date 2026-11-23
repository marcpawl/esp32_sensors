# Simulating the ESP32-S3 test project in Wokwi

Wokwi can run the firmware built by this project. Use a full merged image so
these all match the build:

- bootloader
- partition table
- flash size
- application

## Build the project

From the repository root:

```bash
./scripts/dev.py build components/espidfcpp/tests/test-espidf-cpp-esp32
```

The build writes its images under `build_esp32s3/`, next to this file.

## Merge the flash images

`esptool` is the current command name; `esptool.py` is the old spelling. The
subcommand is `merge-bin`, not `merge_bin`.

The build directory contains `flash_args`. It already lists:

- offsets
- input files
- flash settings

Merge from inside that directory:

```bash
./scripts/dev.py shell -C components/espidfcpp/tests/test-espidf-cpp-esp32/build_esp32s3 \
    esptool --chip esp32s3 merge-bin -o wokwi-merged.bin @flash_args
```

That creates `build_esp32s3/wokwi-merged.bin`.

The expanded form is useful when checking what `flash_args` contains. Run it
from this project directory, inside the ESP-IDF environment:

```bash
esptool --chip esp32s3 merge-bin \
    -o build_esp32s3/wokwi-merged.bin \
    --flash-mode dio --flash-freq 80m --flash-size 2MB \
    0x0 build_esp32s3/bootloader/bootloader.bin \
    0x8000 build_esp32s3/partition_table/partition-table.bin \
    0x10000 build_esp32s3/test-espidf-cpp-esp32.bin
```

## Upload to Wokwi

1. Open the custom-firmware template:
   https://wokwi.com/projects/305457271083631168
2. Replace `diagram.json` with the JSON below.
3. Click in the code editor and press F1.
4. Choose **Upload Firmware and Start Simulation...**.
5. Select `build_esp32s3/wokwi-merged.bin`.

The Windows file picker cannot browse WSL directly. Either paste this into the
address bar:

```text
\\wsl$\Ubuntu\home\marcpawl\github\esp32_sensors\components\espidfcpp\tests\test-espidf-cpp-esp32\build_esp32s3\wokwi-merged.bin
```

or copy the file first:

```bash
cp build_esp32s3/wokwi-merged.bin /mnt/c/Users/Owner/Downloads/
```

## `diagram.json`

TODO(AI!)
Add toggle switch for Config
Add toggle switch for Run
Add green led
Add red led

Use an ESP32-S3 board. The LED on GPIO 5 shows the pin toggled by the
`InterruptHandle` cases, and the UART pins feed the Serial Monitor.

```json
{
  "version": 1,
  "author": "wokwi",
  "editor": "wokwi",
  "parts": [
    {
      "type": "board-esp32-s3-devkitc-1",
      "id": "esp",
      "top": 0,
      "left": 0,
      "attrs": {
        "flashSize": "2"
      }
    },
    {
      "type": "wokwi-led",
      "id": "led1",
      "top": 60,
      "left": 260,
      "attrs": {
        "color": "green"
      }
    }
  ],
  "connections": [
    ["esp:TX", "$serialMonitor:RX", "", []],
    ["esp:RX", "$serialMonitor:TX", "", []],
    ["esp:5", "led1:A", "green", []],
    ["led1:C", "esp:GND.1", "black", []]
  ]
}
```

To use USB CDC instead of UART, add `"serialInterface": "USB_SERIAL_JTAG"` to
the board `attrs` and delete the two `$serialMonitor` connections.

## Run and read the result

1. Press the green play button.
2. Open the Serial Monitor.
3. Wait for the Unity test output.

The runner prints one of these sentinels:

- `ESPIDF_CPP_ESP32_TESTS: PASS`
- `ESPIDF_CPP_ESP32_TESTS: FAIL`

You can also upload `build_esp32s3/test-espidf-cpp-esp32.elf` directly, but
Wokwi then supplies its own bootloader and partition table. The merged image
above is the exact flash layout.
