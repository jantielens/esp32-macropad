---
title: JC1060P470C SD-primary Macropad
description: MicroSD requirements and differences for the JC1060P470C SD target.
---

Target `jc1060p470c-sd` uses the
[JC1060P470C hardware](../jc1060p470c/README.md). Its power warning, native USB
HID connector, UART diagnostics, display behavior, and recovery procedure apply.

## Storage differences

Persistent files use the onboard MicroSD slot instead of internal flash.
Device settings stored in NVS remain in internal flash.

> [!IMPORTANT]
> Insert a readable FAT32 card before boot. A missing or unreadable card stops
> startup; there is no internal-file fallback or automatic formatting. Correct
> the card problem and reboot.

Back up files before changing targets. Internal files are not copied to the
card when this variant is flashed. Power down before removing the card.

The slot uses SDMMC slot 0, four-bit mode at up to 20 MHz, and LDO channel 4.
The board package assigns GPIO39-44 to SDMMC. GPIO45 is reserved for the board
package's startup pulse, not an application-controlled card-power output.

## Other differences

Passive button/pad binding refresh is limited to a 100 ms interval. This does
not change the values stored in bindings.
The base target's other capabilities remain configured, including USB HID
and OTA. First flash or recovery must use this variant's full image.

## References

* [Variant assignments](board_overrides.h)
* [Hardware index](../../../docs/user/hardware.md)
