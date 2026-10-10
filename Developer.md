
Hardware Setup to Laptop
========================

Connect USB3.0-SuperSpeed 4Ports Hub to back left USB port.
Connect USB1 to V.
Use button to disable USB1.
Wait 30 Seconds
Use botton to enable USB1, ESP should have red light.

On host
```
$ dmesg | tail -n 20 | grep USB
[159528.424091] usb 1-1.1: USB disconnect, device number 8
[159530.816063] usb 1-1.1: new full-speed USB device number 9 using xhci_hcd
[159530.916971] usb 1-1.1: New USB device found, idVendor=1a86, idProduct=55d3, bcdDevice= 4.45
[159530.916987] usb 1-1.1: New USB device strings: Mfr=0, Product=2, SerialNumber=3
[159530.916993] usb 1-1.1: Product: USB Single Serial
[159530.925207] cdc_acm 1-1.1:1.0: ttyACM0: USB ACM device
marcpawl@sam:~/github/esp32_sensors$ 

$ esptool --port /dev/ttyACM0
```

Setup the docker environment

```
$ docker compose build
...
$ docker compose up -d

...
$ docker exec -it esp32_sensors-dev-1 bash
...
```

In the container

```
dev@cf552928deb3:/project$ esptool --port /dev/ttyACM0 chip-id
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
dev@cf552928deb3:/project$ 
```

Container is now ready for use with ESP-IDF.

Configure the envioronment

```
$ rm -rf build_esp32s3
$ idf.py -B build_esp32s3 fullclean
$ idf.py -B build_esp32s3 set-target esp32s3
$ 



