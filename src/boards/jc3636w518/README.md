---
title: Guition JC3636W518
description: Round-display limits, USB recovery, and hardware connections for the JC3636W518.
---

Target `jc3636w518` runs Macropad on a 3.6-inch, round 360 x 360 ST77916
QSPI display with CST816S single-touch input. The ESP32-S3 board has 16 MB
flash and 8 MB PSRAM. Shared hardware also applies to the
[SD-primary variant](../jc3636w518-sd/README.md).

## Display and firmware limits

The maximum pad grid is 5 x 5. The circular panel clips the corners of the
square drawing area; keep important labels and controls away from the edges.

The firmware supports Wi-Fi/MQTT, USB HID, and native Extensions. BLE keyboard
output, alarms, MCP, remote image fetching, and audio are disabled.
The chip's Bluetooth capability does not make BLE available in this target.
Keyboard output starts Off; select USB in the portal and reboot.

Files use internal flash in this target. Extensions have two 56 KiB slots and
one 120 KiB slot of usable ELF capacity; install ESP32-S3 packages.

## USB and recovery

The native USB connection is used for keyboard, mouse, and gamepad output,
not a serial console. Use UART0 at 115200 baud for runtime serial diagnostics.
For download recovery, hold BOOT while resetting, release it, then flash
through the [installer](https://jantielens.github.io/esp32-macropad/).

OTA is supported after the first full flash.

> [!IMPORTANT]
> The current layout uses two 4 MiB application slots. When upgrading from
> older firmware with a 3 MiB layout, back up files and Extension packages, then perform
> a full serial flash with the new partition table. OTA alone does not move
> partitions. Restore files/packages afterward.

## Configured onboard signals

These pins are occupied by the display and touch controller; they are not a
spare-pin connector map.

| Signal                      | GPIO          |
| --------------------------- | ------------- |
| Backlight                   | 15            |
| Display CS / clock          | 10 / 9        |
| Display D0 / D1 / D2 / D3   | 11/12/13/14   |
| Display reset               | 47            |
| Touch SDA / SCL             | 7 / 8         |
| Touch interrupt / reset     | 41 / 40       |

## References

* [Firmware assignments](board_overrides.h)
* [Build and partition selections](../../../config.sh)
* [Hardware index](../../../docs/user/hardware.md)
