---
title: Guition JC3248W535
description: Display connections, USB HID, recovery, and partition requirements for the JC3248W535.
---

Target `jc3248w535` is for the ESP32-S3 board with an AXS15231B 320 x 480
QSPI display and integrated capacitive touch. It has 16 MB flash and 8 MB
PSRAM. Match the board model and controller, not just the advertised screen
diagonal. Firmware starts in landscape orientation.

## Firmware support

The target supports Wi-Fi/MQTT, USB keyboard/mouse/gamepad output, MCP, remote
images, and native Extensions. BLE keyboard output, alarms, and audio are
disabled. Keyboard output starts Off; select USB in the portal and reboot.
The touch driver supports up to two contacts.

Persistent files use internal flash. ESP32-S3 native Extensions have two 56 KiB
slots and one 120 KiB slot of usable ELF capacity.

## USB and recovery

The native USB port provides HID output rather than a runtime serial console.
Use UART0 at 115200 baud for serial diagnostics. For recovery, hold BOOT while
resetting, release it, then use the
[installer](https://jantielens.github.io/esp32-macropad/) with this exact target.
BOOT is GPIO0; configured runtime actions do not override its download-mode
behavior at reset.

OTA is supported after initial full flashing.

> [!IMPORTANT]
> The current layout has two 4 MiB application slots. Upgrading from older
> firmware with a 3 MiB layout requires a full serial flash including the new partition
> table, not just OTA. Back up files and Extension packages first and restore
> them afterward because the storage and Extension partitions move.

## Configured onboard signals

These are occupied panel/touch connections, not free expansion pins.

| Signal                      | GPIO            |
| --------------------------- | --------------- |
| BOOT                        | 0, active low   |
| Backlight                   | 1               |
| Display CS / clock          | 45 / 47         |
| Display D0 / D1 / D2 / D3   | 21/48/40/39     |
| Display TE                  | 38              |
| Touch SDA / SCL             | 4 / 8           |

Touch I2C runs at 400 kHz. No dedicated display reset or touch interrupt/reset
GPIO is configured.

## References

* [Firmware assignments](board_overrides.h)
* [Build and partition selections](../../../config.sh)
* [Hardware index](../../../docs/user/hardware.md)
