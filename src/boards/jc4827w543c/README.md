---
title: Guition JC4827W543C
description: Capacitive-touch model identification, USB recovery, and no-OTA limits for the JC4827W543C.
---

Target `jc4827w543c` requires the **capacitive C model**: ESP32-S3 N4R8 with
4 MB flash, 8 MB PSRAM, a 4.3-inch 480 x 272 NV3041A QSPI display, and GT911
touch. Do not use it for the similarly named resistive-touch model.

## Firmware limits

The target supports touchscreen pads and Wi-Fi/MQTT. To fit the application,
BLE keyboard output, MCP, remote image fetching, native Extensions, and audio
are disabled. USB HID is not enabled; the native USB port is configured for
hardware CDC serial.

Files use internal flash.

> [!IMPORTANT]
> This target uses a single large application partition and does not support
> OTA firmware updates. Use a USB full flash for installation, recovery, and
> subsequent firmware updates. Back up settings and files before flashing.

Use the [installer](https://jantielens.github.io/esp32-macropad/) with this exact
target. If necessary, hold BOOT while resetting, then release it to enter
download mode. Diagnostic serial runs at 115200 baud.

## Configured onboard signals

These assignments are occupied by the display and touch controller.

| Signal                      | GPIO          |
| --------------------------- | ------------- |
| Backlight                   | 1             |
| Display CS / clock          | 45 / 47       |
| Display D0 / D1 / D2 / D3   | 21/48/40/39   |
| Touch SDA / SCL             | 8 / 4         |
| Touch reset / interrupt     | 38 / 3        |

GT911 uses address `0x5D`, with `0x14` as the alternate. This touch wiring
differs from other Guition boards with similar display pins.

## References

* [Board support reference](https://github.com/wegi1/ESP32-S3-JC4827W543C)
* [Firmware assignments](board_overrides.h)
* [Build and partition selections](../../../config.sh)
* [Hardware index](../../../docs/user/hardware.md)
