---
title: reTerminal E1003 Interactive
description: Touch connections, render modes, Extension storage, and flashing requirements for interactive E1003 firmware.
---

Target `reterminal-e1003-interactive` uses the
[reTerminal E1003 hardware](../reterminal-e1003-frame/README.md) for
touch-operated pads. It does not use the sleep-first Frame lifecycle.
The shared page owns panel geometry, CH340 USB/UART wiring, and onboard
power/display assignments; its Frame wake actions and SD cache do not apply
to this target.

## Touch and display

The GT911 is at I2C address `0x5D`, using SDA GPIO19, SCL GPIO20, reset
GPIO16, and interrupt GPIO2. These lines are shared physically with onboard
peripherals, not free accessory connections.

Firmware starts in portrait orientation with 16-level grayscale rendering.
Black-and-white is also available. Save mode changes and reboot.
Updates are coalesced, animations are disabled, and visible waveforms take
time. Prefer slowly changing pads/widgets over rapid animation or live video.
Regional grayscale quality and timing still require physical validation.

The board default VCOM is **-1.40 V** (`EPAPER_DEFAULT_VCOM_MV=1400`).
The interactive driver consumes the saved
[VCOM setting](../../../docs/user/specialized/interactive-epaper.md#configure-panel-vcom)
after restart; leave it at the board default unless the panel specifies otherwise.

There is no frontlight. Audio, remote image fetching, and the image library
are disabled. Wi-Fi/MQTT, MCP, and native Extensions are configured.
The CH340 connection is for flashing/serial logs, not USB HID.

## Storage, Extensions, and firmware updates

Files use internal flash. The target supports OTA with two 8 MiB application
slots. Native Extensions require ESP32-S3 packages and offer two 56 KiB slots
and one 120 KiB slot of usable ELF capacity.
Prefer slow-changing Extension widgets on e-paper.

> [!WARNING]
> This layout reserves the final 256 KiB of flash for Extensions and makes
> the filesystem smaller than older E1003 layouts. Mounting an older, larger
> filesystem can cause a reboot loop. Back up all files before switching.

When migrating from the older layout, back up settings and files, erase the
old storage volume, and install the full firmware/partition layout over USB
before restoring the files. A firmware-only OTA does not resize partitions.
Do not erase the old volume until its backup is safe.

Use this exact target in the
[installer](https://jantielens.github.io/esp32-macropad/) and retain its CH340
UART build settings, not native USB CDC. Regular updates on the same partition
layout can use OTA.

## References

* [Variant assignments](board_overrides.h)
* [Interactive display driver](../../app/drivers/it8951_epaper_driver.cpp)
* [Hardware index](../../../docs/user/hardware.md)
