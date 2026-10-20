---
title: Cheap Yellow Display v2
description: Exact CYD model, touch wiring, and firmware limits for the one-USB ESP32-2432S028R.
---

Target `cyd-v2` is for the one-USB v2 ESP32-2432S028R: a classic ESP32 with
4 MB flash, no PSRAM, a 2.8-inch 320 x 240 ILI9341 display, and XPT2046
resistive touch. Other CYD revisions and capacitive-touch products are not
interchangeable targets.

## Firmware support and limits

The firmware supports Wi-Fi/MQTT, BLE keyboard output, MCP, and remote image
fetching. There is no native USB HID, audio, or native Extension support.
The USB connector is for power, UART flashing, and serial diagnostics, not
keyboard output. BLE keyboard output starts Off; enable it in the portal and
reboot before pairing.

MQTT triggers are limited to three on this low-memory target. Avoid large
images and memory-heavy pads. A physical SD slot, RGB LED, or light sensor
does not imply firmware support: these peripherals are not enabled here.

Files use internal flash. OTA is supported after the first full flash.
Use the [installer](https://jantielens.github.io/esp32-macropad/) with the exact
target and a data-capable USB cable. If automatic flashing fails, use the
board's BOOT/reset download sequence; runtime serial logs use 115200 baud.

## Display and touch

The resistive panel reports a single contact and benefits from the portal's
touch calibration. Firmware already configures panel gamma and inversion;
do not apply unrelated TFT_eSPI settings from a different CYD revision.

The display and touch use separate SPI connections. These are occupied onboard
signals, not spare GPIOs:

| Signal                        | GPIO           |
| ----------------------------- | -------------- |
| Display MISO / MOSI / SCK     | 12 / 13 / 14   |
| Display CS / DC / backlight   | 15 / 2 / 21    |
| Touch MISO / MOSI / clock     | 39 / 32 / 25   |
| Touch CS / interrupt          | 33 / 36        |

No dedicated display reset GPIO is configured.

## References

* [CYD hardware reference](https://github.com/witnessmenow/ESP32-Cheap-Yellow-Display)
* [Firmware assignments](board_overrides.h)
* [Build and partition selections](../../../config.sh)
* [Hardware index](../../../docs/user/hardware.md)
