---
title: Inkplate 5 V2 Frame
description: Wake controls, panel calibration, and firmware limits for the Inkplate 5 V2 frame.
---

Target `inkplate5v2-frame` is for the Soldered Inkplate 5 V2 with a classic
ESP32, 4 MB flash, 4 MB PSRAM, and a 5.17-inch 720 x 1280 grayscale panel.

## Firmware behavior

This is a sleep-first E-Paper Frame, not an interactive Macropad. It wakes,
connects to Wi-Fi, refreshes a configured image, and returns to deep sleep.
MQTT and MCP controls are available while it is awake and connected.
There are no interactive pads, touch controls, audio, or BLE keyboard output.
The board has no frontlight.

The frame driver accepts JPEG, PNG, and BMP images and displays up to eight
gray levels. Prepare images for the panel geometry and chosen rotation rather
than expecting a general-purpose browser or dashboard renderer.

## Wake and setup

The carrier's WAKE button is GPIO36, active low, with an external pull-up.

* Short press: wake and force an image refresh.
* Hold for 2.5 seconds: enter configuration mode.
* The configuration portal's default idle timeout is five minutes.

Use the [installer](https://jantielens.github.io/esp32-macropad/) with the exact
target for the first full USB flash and partition table. OTA is supported
afterward. Back up settings/files before changing firmware layouts.

## VCOM and panel wiring

The panel is driven through InkplateLibrary, which owns its parallel pin map
and power sequencing. GPIO36 is the wake input, not a free expansion output.
Use the vendor's Inkplate 5 V2 schematic for connector and panel wiring;
do not substitute pinouts from another Inkplate model.

The portal provides TPS65186 VCOM calibration. Use the panel-specific value
printed on the ribbon cable as the reference. Preview with **Show test
pattern** before committing a change. **Write VCOM** programs EEPROM; do not
repeatedly write it as part of normal refreshes or copy a different panel's
calibration.

## References

* [E-Paper Frame workflow](../../../docs/user/specialized/epaper-frame/README.md)
* [Firmware assignments](board_overrides.h)
* [Shared Inkplate frame driver](../../app/device_classes/epaper_frame/drivers/inkplate_driver.cpp)
* [Hardware index](../../../docs/user/hardware.md)
