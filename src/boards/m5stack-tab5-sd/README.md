---
title: M5Stack Tab5 SD-primary Macropad
description: MicroSD requirements for the M5Stack Tab5 SD-primary firmware.
---

Target `m5stack-tab5-sd` uses the same
[Tab5 hardware and display revision requirements](../m5stack-tab5/README.md)
as the flash-primary target. Physical validation is required.

## Storage

Pad files, icons, sounds, and other persistent files use the onboard microSD
card instead of internal flash. Device settings in NVS remain in flash.

> [!IMPORTANT]
> Insert a readable FAT32 card before boot. Startup stops if the card cannot
> mount; there is no fallback to internal file storage or automatic formatting.
> Keep the card installed while the device is running.

Back up files before switching variants. Firmware installation does not copy
files between internal storage and the card. The card uses SPI at up to 25 MHz:
CS GPIO42, SCK GPIO43, MOSI GPIO44, and MISO GPIO39.

## References

* [Variant assignments](board_overrides.h)
* [Hardware index](../../../docs/user/hardware.md)