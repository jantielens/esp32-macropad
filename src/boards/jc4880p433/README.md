---
title: Guition JC4880P433
description: Hardware connections, recovery, and firmware variants for the JC4880P433 touchscreen board.
---

Use target `jc4880p433` for the standard Macropad on the 4.3-inch,
480 x 800 ST7701 MIPI-DSI display with GT911 touch. The board has an ESP32-P4,
16 MB flash, and 32 MB PSRAM. Wi-Fi uses its ESP32-C6 companion.

This is the shared hardware reference for this family. Variant-specific
connections and firmware differences are documented in each variant below.

## Power, USB, and recovery

Use the native USB device connector for USB keyboard, mouse, and gamepad output.
Use the USB-UART/download connector for flashing and serial diagnostics at
115200 baud. The standard target does not expose a native USB serial console.
Confirm connector labels on your board; a host-only connector cannot provide
HID output.

> [!WARNING]
> Do not assume two powered USB connections are safe. Check the board's
> power-routing documentation before connecting multiple power sources.

For serial recovery, hold BOOT while resetting, then release BOOT to flash the
exact target with the
[installer](https://jantielens.github.io/esp32-macropad/). Install the full image
and partition table on first flash or when changing partition layouts.
Later updates can use OTA. Back up files and configuration before changing
firmware variants; changing storage backend does not migrate existing files.

## Standard firmware

The standard target uses internal flash for files and supports Wi-Fi/MQTT,
USB or BLE keyboard output, MCP, remote images, native Extensions, ES8311
speaker/MP3 playback, and an OV02C10 camera when fitted.
Keyboard output starts Off; select the supported transport in the portal and
reboot. Hardware camera availability depends on the attached module.

BOOT can also host runtime tap/hold actions. It is a boot-strapping input:
holding it during reset enters download mode, not an ordinary button action.

The Extension partition provides two 56 KiB slots and one 120 KiB slot.
Packages must target ESP32-P4. These are usable ELF capacities, not an upload
package's total byte size.

## Configured onboard signals

These are firmware assignments, not a list of spare expansion pins. Check the
board revision and schematic before attaching accessories.

| Signal                             | GPIO or address            |
| ---------------------------------- | -------------------------- |
| BOOT                               | 35, active low             |
| Panel reset / backlight            | 5 / 23                     |
| Shared I2C SDA / SCL               | 7 / 8                      |
| GT911 address                      | `0x5D`, alternate `0x14`   |
| Camera control address             | `0x36`                     |
| ES8311 address                     | `0x18`                     |
| Audio MCLK / BCLK / LRCK           | 13 / 12 / 10               |
| Audio data to codec / amp enable   | 9 / 11                     |

The standard firmware does not enable microphone capture on this board.
External scale and metering connections are defined by the specialized variants,
not by the display's shared I2C bus.

## Firmware variants

* [SD-primary Macropad](../jc4880p433-sd/README.md)
* [NAU7802 Coffee Scale](../jc4880p433-nau7802/README.md)
* [HX711 Coffee Scale](../jc4880p433-hx711/README.md)
* [Darkroom Timer](../jc4880p433-darkroom/README.md)
* [Shutter Tester](../jc4880p433-shutter/README.md)

## References

* [Board support reference](https://github.com/csvke/esp32_p4_jc4880p433c_bsp)
* [Firmware assignments](board_overrides.h)
* [Build and partition selections](../../../config.sh)
* [Hardware index](../../../docs/user/hardware.md)
