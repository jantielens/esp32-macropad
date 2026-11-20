---
title: Waveshare LCD4B SD-primary Macropad
description: MicroSD requirements and storage differences for the LCD4B SD firmware.
---

Target `esp32-p4-lcd4b-sd` uses the
[Waveshare LCD4B hardware](../esp32-p4-lcd4b/README.md), including its
microphone, display, USB connections, flash limit, and recovery procedure.

## Differences from the standard target

Pad files, voice notes, icons, sounds, and other persistent files use the
onboard MicroSD slot instead of internal flash. Device settings stored in NVS
remain in internal flash.

> [!IMPORTANT]
> Insert a readable FAT32 card before boot. Startup stops if the card cannot
> mount; there is no fallback to internal file storage or automatic formatting.
> Check the card and reboot after correcting the problem.

Back up files before switching targets. Flashing this variant does not copy
files from internal storage to the card. Keep the card installed during use,
and power down before removing it.

The slot uses four-bit SDMMC on GPIO39-44, with GPIO45 and LDO channel 4
configured for card power. Long recordings and simultaneous Wi-Fi use still
require hardware validation.

Use this exact target for the initial full flash. OTA is supported afterward.

## References

* [Variant assignments](board_overrides.h)
* [Voice Notes](../../../docs/user/media/voice-notes.md)
* [Hardware index](../../../docs/user/hardware.md)
