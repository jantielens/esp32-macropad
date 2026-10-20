---
title: JC3636W518 SD-primary Macropad
description: MicroSD wiring and persistent-storage requirements for the round JC3636W518.
---

Target `jc3636w518-sd` uses the
[JC3636W518 hardware](../jc3636w518/README.md). Its display limits, USB HID
behavior, unavailable features, Extension capacities, and recovery procedure
remain applicable.

## Storage differences

Persistent files use the onboard MicroSD slot. Device settings stored in NVS
remain in internal flash.

> [!IMPORTANT]
> Insert a readable FAT32 card before boot. Startup stops if it cannot mount
> the card, without automatic formatting or fallback to internal file storage.
> Check the card and reboot.

Switching from the base target does not copy internal files to SD. Back up
before switching and restore the files to the card. Power down before removing it.

## Card connections

The slot uses one-bit SDMMC at up to 20 MHz, not SPI.

| Signal   | GPIO   |
| -------- | ------ |
| D0       | 2      |
| Clock    | 3      |
| CMD      | 4      |

Use the variant's full image/partition table for first installation or layout
changes. OTA remains supported. The base board's older-layout migration warning
also applies.

## References

* [Variant assignments](board_overrides.h)
* [Hardware index](../../../docs/user/hardware.md)
