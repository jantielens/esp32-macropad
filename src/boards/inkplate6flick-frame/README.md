---
title: Inkplate 6FLICK Frame
description: Shared Inkplate 6FLICK hardware, frame wake controls, and calibration notes.
---

The Soldered Inkplate 6FLICK has a classic ESP32, 4 MB flash, 8 MB PSRAM,
and a 6-inch 1024 x 758 grayscale panel. Hardware includes a capacitive
touchscreen and frontlight.

Target `inkplate6flick-frame` is the sleep-first image-frame firmware.
For touch-operated pads and frontlight controls, use
[Interactive firmware](../inkplate6flick-interactive/README.md).
This page is the shared physical-hardware reference for both targets.

## Frame firmware

The frame wakes, joins Wi-Fi, optionally fetches an image, refreshes, and
returns to deep sleep. MQTT and MCP are available while awake and connected.
JPEG, PNG, and BMP images can be rendered in eight gray levels.
Prepare images for the panel size and selected rotation.

The frame target intentionally does not use the physical touchscreen or
frontlight. It has no interactive pads, audio, or BLE keyboard output.
This is a firmware distinction, not missing touch/frontlight hardware.

## Wake, recovery, and storage

WAKE is GPIO36, active low, with an external pull-up on the carrier.
A short press wakes and forces a refresh; holding for 2.5 seconds enters
configuration mode. The default portal idle timeout is five minutes.

Use the [installer](https://jantielens.github.io/esp32-macropad/) and the exact
target for the first full USB flash. The frame layout supports OTA with
approximately 1.9 MiB application slots.

> [!IMPORTANT]
> Frame and Interactive firmware use different partition layouts. Back up
> settings, files, and any Extension packages before switching. Use a full
> USB flash including the partition table, not an OTA-only application image,
> and restore the data afterward.

## Hardware and VCOM

InkplateLibrary owns the panel, touch, frontlight, and power-control wiring.
GPIO36 is an input-only wake connection, not a free output.
Consult the vendor's 6FLICK schematic for accessory connectors; do not use
another Inkplate model's full pinout.

The frame portal exposes TPS65186 VCOM calibration. Start with the value
printed on this panel's ribbon cable. Preview a candidate with **Show test
pattern** before using **Write VCOM**, which programs EEPROM. Do not repeatedly
write VCOM during normal operation or copy another panel's value.

## References

* [E-Paper Frame workflow](../../../docs/user/specialized/epaper-frame/README.md)
* [Frame assignments](board_overrides.h)
* [Shared Inkplate frame driver](../../app/device_classes/epaper_frame/drivers/inkplate_driver.cpp)
* [Hardware index](../../../docs/user/hardware.md)
