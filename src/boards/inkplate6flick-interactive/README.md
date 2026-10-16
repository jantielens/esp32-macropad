---
title: Inkplate 6FLICK Interactive
description: Touch, frontlight, rendering, storage, and update limits of the interactive 6FLICK target.
---

Target `inkplate6flick-interactive` uses the
[Inkplate 6FLICK hardware](../inkplate6flick-frame/README.md), but runs
interactive pads rather than the sleep-first Frame lifecycle.

## Touch and display behavior

Touchscreen input and frontlight brightness controls are enabled. Brightness
zero turns the frontlight off without changing the e-paper image.
The frontlight timeout controls illumination; it does not erase the panel.

The default black-and-white mode supports partial refreshes, with full
refreshes for initial presentation and ghosting control. Selecting grayscale
provides eight gray levels but requires a full waveform on every physical
update. Save render-mode changes and reboot.

Animations are disabled. Use slow-changing bindings/widgets and expect visible
refresh delays. Screen-saver behavior is frontlight-only rather than a
conventional LCD power-down.

## Storage and firmware limits

Wi-Fi/MQTT and one ESP32 native Extension are supported. MCP, BLE keyboard
output, audio, remote image fetching, the image library, and Home Assistant
history are disabled. The built-in touchscreen does not imply those features.

> [!IMPORTANT]
> This layout has a single 3.5625 MiB application partition, a 280 KiB internal
> filesystem, and one Extension slot with 32 KiB usable ELF capacity.
> It does **not** support OTA firmware updates.

Use a full USB flash for installation and every firmware update. The package
must match the classic ESP32 Extension ABI, not ESP32-S3 or ESP32-P4.
Keep stored files small enough for the limited filesystem.

Before switching from Frame firmware or an older layout, back up settings,
files, and Extension packages. The partitions move and the filesystem shrinks;
the old volume is not preserved. Flash the full partition layout and restore
the backup afterward.

## References

* [Variant assignments](board_overrides.h)
* [Interactive display driver](../../app/drivers/inkplate6flick_epaper_driver.cpp)
* [Hardware index](../../../docs/user/hardware.md)
