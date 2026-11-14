---
title: reTerminal Sticky Interactive SD
description: MicroSD requirements for the reTerminal Sticky SD-primary firmware.
---

Target `reterminal-sticky-interactive-sd` uses the same
[Sticky hardware and recovery controls](../reterminal-sticky-interactive/README.md)
as the flash-primary target. SD and display coexistence require physical
validation.

## Storage

Pad files, icons, voice clips, and other persistent files use microSD instead
of internal flash. Device settings in NVS remain in flash.

> [!IMPORTANT]
> Insert a readable FAT32 card before boot. An MBR partition scheme is
> recommended. Startup stops if the card cannot mount; there is no internal
> storage fallback or automatic formatting. Keep the card installed while
> the device is running.

Shutdown is rejected while a file write is active; finish or cancel the
operation and try again. An unfinished voice recording is discarded during
shutdown. Unexpected power loss can still damage the filesystem.

Back up files before switching variants. Firmware installation does not copy
files between internal storage and the card. SD shares SPI clock, MOSI, and
MISO with the display; it uses GPIO8 for chip select and GPIO10 for active-high
power enable. The SD SPI clock is 1 MHz.
