---
title: reTerminal E1003 Frame
description: Shared E1003 hardware, frame controls, image geometry, USB, and optional SD cache.
---

The Seeed reTerminal E1003 has an ESP32-S3, 32 MB flash, 8 MB PSRAM, and a
10.3-inch IT8951 e-paper panel with 16 gray levels. The physical portrait
geometry is 1404 x 1872; firmware drives a native 1872 x 1404 landscape
framebuffer before rotation.

Target `reterminal-e1003-frame` is sleep-first image-frame firmware.
For touch-operated pads, use
[Interactive firmware](../reterminal-e1003-interactive/README.md).
This page owns their shared physical-hardware notes.

## Frame behavior and image requirements

The frame wakes, connects to Wi-Fi, refreshes, then deep-sleeps.
MQTT and MCP are available while awake and connected. Runtime touch and
ordinary pad navigation are not enabled in Frame firmware. Audio and BLE
keyboard output are disabled. The panel has no frontlight.

Use G16P, G16Z, or **baseline JPEG** images at the native **1872 x 1404**
geometry. Progressive JPEG, PNG, and BMP are not supported by this frame
driver. Do not assume formats supported by another frame board will work.
The driver does not scale image downloads.

## Buttons, USB, and recovery

The green/right **KEY0** button (GPIO3) wakes and forces a refresh on a short
press. Hold for 2.5 seconds to enter configuration mode.
The middle KEY1 (GPIO4) and left KEY2 (GPIO5) have no runtime Frame actions.
The portal's default idle timeout is five minutes.

The USB connector uses a CH340 UART bridge, with UART0 TX/RX on GPIO43/44.
It is **not native USB CDC or a USB HID connector**. Serial diagnostics run
at 115200 baud. When building firmware, retain this target's UART settings;
enabling native USB CDC will not route logs to the CH340.

Use the [installer](https://jantielens.github.io/esp32-macropad/) for a full
first USB flash and the vendor's download-mode procedure if recovery is needed.
The frame layout supports OTA with two 8 MiB application slots.
Back up settings and files before changing variants or partition layouts.

## Optional MicroSD cache

The card is an optional cache for Service-mode images and its offline queue,
not the primary filesystem for settings or pads. This differs from an
SD-primary Macropad target. Frame operation without the cache remains possible;
offline queued images require the cache.

| Signal                         | GPIO           |
| ------------------------------ | -------------- |
| Shared SPI SCK / MISO / MOSI   | 7 / 8 / 9      |
| Card CS / power / detect       | 14 / 39 / 15   |
| IT8951 CS / busy / reset       | 10 / 13 / 12   |
| Panel / controller enable      | 11 / 21        |
| Battery ADC / divider enable   | 1 / 40         |

These are onboard assignments, not free expansion pins. The battery divider
ratio is 2:1. IT8951 manages its panel bias internally; the TPS65186 portal
VCOM calibration used by Inkplate boards is not available.

## References

* [E-Paper Frame workflow](../../../docs/user/specialized/epaper-frame/README.md)
* [Frame assignments](board_overrides.h)
* [Frame driver and image validation](../../app/device_classes/epaper_frame/drivers/reterminal_e1003_driver.cpp)
* [Build and partition selections](../../../config.sh)
* [Hardware index](../../../docs/user/hardware.md)
