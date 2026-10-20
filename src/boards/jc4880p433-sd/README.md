---
title: JC4880P433 SD-primary Macropad
description: MicroSD requirements and storage differences for the JC4880P433 SD firmware.
---

Target `jc4880p433-sd` uses the
[JC4880P433 hardware](../jc4880p433/README.md), including its USB, power,
display, audio, camera, and recovery connections.

## Differences from the standard target

Pad files, icons, sounds, and other persistent files use the onboard MicroSD
slot instead of internal flash. Device settings stored in NVS remain in
internal flash. Other standard Macropad capabilities, including USB HID, remain
available.

> [!IMPORTANT]
> Insert a readable FAT32 card before boot. Startup stops if the card cannot
> mount; there is no fallback to internal file storage or automatic formatting.
> Check the card and reboot after correcting the problem.

Back up files before switching targets. Flashing this variant does not copy
files from internal storage to the card. Keep the card installed during use,
and power down before removing it.

## Card interface

The onboard slot uses SDMMC slot 0, four data lines, and a 20 MHz limit.
GPIO45 is the active-low card-power control; the slot uses LDO channel 4.
The SD data pins follow the ESP32-P4 board package's slot-0 mapping, not a
separately wired SPI card reader.

Use the base board's download procedure for first installation and recovery.
OTA remains supported.

## References

* [Variant assignments](board_overrides.h)
* [Hardware index](../../../docs/user/hardware.md)
