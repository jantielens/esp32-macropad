---
title: FeatherS3[D] Interactive E-Paper
description: Wiring, power, button configuration, render modes, and flashing requirements for FeatherS3[D] with the Waveshare 7.8-inch IT8951 HAT.
---

Target `feathers3d-interactive` pairs an Unexpected Maker FeatherS3[D]
(ESP32-S3, 16 MB flash, 8 MB QSPI PSRAM) with the Waveshare 7.8-inch
1872 x 1404 IT8951 e-paper HAT. It runs existing pads and widgets in
landscape, with no touch or frontlight.

This is always-on interactive Macropad firmware, not the sleep-first
E-Paper Frame. Use the HAT with its IT8951 controller, not a raw panel
connected directly to the Feather.

## Wiring and power

The following table uses ESP32 GPIO numbers, not connector positions or
Feather header aliases. Connect the HAT's SPI interface and select SPI mode
on its interface switch before powering it.

| HAT signal | Feather connection | Notes |
| ---------- | ------------------ | ----- |
| 5V | External regulated 5 V supply | Always powered |
| GND | Feather GND and supply GND | Common ground |
| SCK | GPIO36 | SPI clock |
| MOSI | GPIO35 | Feather output to HAT |
| MISO | GPIO37 | HAT output to Feather |
| CS | GPIO10 | Active LOW |
| RST | GPIO38 | Active LOW reset |
| HRDY | GPIO5 | LOW means busy, HIGH means ready |

| Other signal | Connection | Notes |
| ------------ | ---------- | ----- |
| Hardware button | GPIO14 to GND | Normally-open contact, active LOW |
| Built-in blue LED | GPIO13 | Onboard, active HIGH |

> [!WARNING]
> The HAT requires 5 V power and 3.3 V logic. Do not power it from the
> Feather's 3.3 V regulator or apply 5 V to ESP32 GPIOs. Turn power off
> before connecting the panel or changing its cables.

GPIO17 is unused. This profile does not control a boost converter or
switch the HAT's power rail. Power the Feather through its normal USB-C
connection and provide the HAT's 5 V supply with a common ground.

The default panel VCOM is **-1.53 V** (`EPAPER_DEFAULT_VCOM_MV=1530`), confirmed
for this panel. Check the panel label before substituting another panel;
do not assume the same VCOM applies. Change it through
[VCOM configuration](../../../docs/user/specialized/interactive-epaper.md#configure-panel-vcom)
and restart. Initialization verifies VCOM readback.
Rotation 2 matches the confirmed landscape mounting.

## Button and recovery

The GPIO14 button starts with no tap or hold actions. Assign actions in the
portal's hardware-button configuration, such as pad navigation or a full
display refresh.

For configuration recovery, hold this button through the application's
startup check for at least 1.5 seconds. This is separate from the Feather's
BOOT button and its chip download-mode function.

## Display modes and firmware limits

Grayscale is the default, with 16 gray levels. B/W is also available;
save render-mode changes in the portal and reboot to activate them.
B/W uses regional DU updates and grayscale uses regional GC16 updates.
Initial presentation, explicit full refresh, and periodic cleanup use
full-panel GC16. The default cleanup threshold is 10 regional updates.

Updates are coalesced and animations are disabled. Prefer slowly changing
pads and widgets; regional updates do not guarantee flicker-free operation.
The single RGB565 LVGL draw buffer holds 32 display lines (119,808 bytes),
allocated preferentially in PSRAM. At native landscape resolution, a full-screen
render needs about 44 flushes instead of 351 with a four-line buffer.
The physical orientation, refresh latency, grayscale ghosting, and button
behavior still need validation with this firmware on hardware.

Wi-Fi/MQTT, MCP, and native Extensions are enabled. Use signed ESP32-S3
(`-s3.ext`) packages and prefer slow-changing Extension widgets on e-paper.
Audio, USB/BLE keyboard output, SD storage, remote image fetching, the image
library, battery telemetry, and deep-sleep operation are not enabled by this
profile.

## Build, flashing, and storage

Build from the repository root:

```bash
./tools/install-custom-partitions.sh
./build.sh feathers3d-interactive
```

The target uses the native `esp32:esp32:um_feathers3` board definition,
hardware USB CDC for serial diagnostics, and enabled QSPI PSRAM.
Files use internal flash through LittleFS. The `ota_4mb_16MB_ext` partition
layout provides:

| Region | Size |
| ------ | ---- |
| OTA firmware slots | 4 MiB each, two slots |
| LittleFS storage | 7.625 MiB |
| Native Extensions | 256 KiB, three slots |

The Extension slots have usable ELF capacities of 56 KiB, 56 KiB, and
120 KiB. Slot metadata occupies the remaining space.

> [!WARNING]
> Migrating from this target's earlier `default_16MB` layout moves the
> firmware and filesystem boundaries and adds the Extensions partition.
> Back up settings and files before installing the full layout over USB.
> Reinitialize storage and restore the backup afterward. A firmware-only
> OTA cannot migrate the partition table or preserve the old filesystem
> at its new location.

For a first full USB flash or recovery, select this exact target in the
[firmware installer](https://jantielens.github.io/esp32-macropad/).
Connect USB-C to the Feather, not the HAT's USB interface.
Back up settings and files before changing firmware or partition layouts.
A firmware-only OTA does not replace the partition table.

## References

* [FeatherS3[D] product](https://unexpectedmaker.com/shop.html#!/FeatherS3-D/p/759221736)
* [Waveshare 7.8-inch e-Paper HAT guide](https://www.waveshare.com/wiki/7.8inch_e-Paper_HAT)
* [Firmware assignments](board_overrides.h)
* [Build and partition selections](../../../config.sh)
* [Interactive e-paper implementation and bring-up](../../../docs/dev/interactive-epaper.md#feathers3d-with-waveshare-78-hat)
* [Shared IT8951 display driver](../../app/drivers/it8951_epaper_driver.cpp)
* [Native Extensions](../../../docs/dev/extensions.md)
* [Hardware index](../../../docs/user/hardware.md)
