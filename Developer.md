# Developer Environment

## Terminolgy

host: Computer running docker.  Physical connections are made from
the host to the dev board.

dev board: ESP32 development board

container: Docker container used for development.  Contains the
ESP-IDF toolset.

IDE: Integrated Development Environment.  CLion instance running
on the host that will connect to the container for development.

## Hardware Setup to Laptop

Connect the USB 3.0 SuperSpeed 4-Port Hub to the front-left USB-C port.

- Connect hub USB1 to **S** (Serial) on the ESP32 dev board.
- Connect hub USB2 to **V** (Volt) on the ESP32 dev board.

Use the buttons on the hub to disable USB1 and USB2.

Wait 30 seconds.

Use the buttons on the hub to enable
USB1 and USB2; the ESP32 dev board should have a red light.

On the host, verify that the ESP dev board is seen:

```console
$ dmesg | tail -n 20 | grep USB
[159528.424091] usb 1-1.1: USB disconnect, device number 8
[159530.816063] usb 1-1.1: new full-speed USB device number 9 using xhci_hcd
[159530.916971] usb 1-1.1: New USB device found, idVendor=1a86, idProduct=55d3, bcdDevice= 4.45
[159530.916987] usb 1-1.1: New USB device strings: Mfr=0, Product=2, SerialNumber=3
[159530.916993] usb 1-1.1: Product: USB Single Serial
[159530.925207] cdc_acm 1-1.1:1.0: ttyACM0: USB ACM device
$
```

Optionally if ESP-IDF is installed on the host:
```console
$ esptool --port /dev/ttyACM0
```


## Setup the Docker Environment

Build the image and start the container:

```console
$ ./scripts/dev.py image
...
$ docker compose up -d
...
$ ./scripts/dev.py shell esptool --port /dev/ttyACM0 chip-id
esptool v5.4.0
Connected to ESP32-S3 on /dev/ttyACM0:
Chip type:          ESP32-S3 (QFN56) (revision v0.2)
Features:           Wi-Fi, BT 5 (LE), Dual Core + LP Core, 240MHz, Embedded PSRAM 8MB (AP_3v3)
Crystal frequency:  40MHz
MAC:                a4:cb:8f:d9:83:68

Stub flasher running.

WARNING: ESP32-S3 has no chip ID. Reading MAC address instead.
MAC:                a4:cb:8f:d9:83:68

Hard resetting via RTS pin...
$
```

The container is now ready for use with ESP-IDF.

Configure the environment:

```console
$ rm -rf mcgowan/build_esp32s3
$ ./scripts/dev.py shell -C mcgowan idf.py -B build_esp32s3 fullclean
$ ./scripts/dev.py shell -C mcgowan idf.py -B build_esp32s3 set-target esp32s3
$
```

## CLion

Version: CLion 2026.2

### CLion IDE Usage

Change the CLion registry as described in
[SUPPORT-A-258](https://youtrack.jetbrains.com/articles/SUPPORT-A-258) to not
download the IDE when connecting to dev containers.

Open a CLion project at the root of this project.

Then use **File | Remote Development...**:

1. Select **Dev Containers** in the left bar.
2. Select the existing container if it exists. Otherwise:
   - Select **New Dev Container**.
   - Select the **From Local Project** tab.
     - Select **Docker** in the pull-down.
     - Select the devcontainer file for **Path to devcontainer.json**.
     - Select the **Build container and continue** button.
       - In **Building Dev Container**, select the **Continue** button.
   - Select the **Start IDE and Connect** button.

A new CLion window will appear using the container as the development
environment.

### CLion Configuration Notes

**Docker toolchain:** CLion's Docker toolchain integration is first-class and
officially supported. Configure it under **Settings | Build, Execution,
Deployment | Toolchains → Add → Docker**.

- Point CLion at the standard Docker socket (`unix:///var/run/docker.sock`).
- Select the `espressif/idf:release-v6.1` image.
- CLion will build and run commands inside the container transparently.

**ESP-IDF environment in CLion:** Inside the Docker container, ESP-IDF's
environment is already sourced by the image entrypoint, so the environment-file
mechanism is typically unnecessary. If CLion complains about missing `idf.py`,
point its **Environment file** setting at `/opt/esp/idf/export.sh` (path inside
the image) or leave it blank and rely on the container's entrypoint.

**Serial monitor in CLion:** Pass the serial device through to the container
with `--device=/dev/ttyUSB0` (add this to the CLion Docker toolchain's container
options). Alternatively, use CLion's built-in terminal to run `idf.py monitor`.

**Recommended workflow:** Use CLion for editing, code navigation, and CMake
configuration; use CLion's built-in terminal (`Alt+F12`) for `idf.py`
operations (build, flash, monitor, menuconfig).
