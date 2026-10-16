---
title: Waveshare ESP32-P4-WIFI6-Touch-LCD-4B
description: Connections, USB recovery, audio support, and flash limits for the Waveshare LCD4B.
---

Target `esp32-p4-lcd4b` runs the standard Macropad on a 4-inch, 720 x 720
ST7703 MIPI-DSI display with GT911 touch. Hardware includes an ESP32-P4,
an ESP32-C6 wireless companion, 32 MB flash, and 32 MB PSRAM.
The [Voice Assistant variant](../esp32-p4-lcd4b-voice/README.md) shares this
hardware.

## USB, power, and recovery

Use the **USB OTG** connector for keyboard, mouse, and gamepad output and
**USB TO UART** for flashing and serial logs at 115200 baud.
The standard target does not expose native USB serial logs. Keyboard output
starts Off; select USB or BLE in the portal and reboot.

> [!WARNING]
> Do not assume simultaneous powered USB cables are safe. Check the vendor's
> power-routing requirements before connecting multiple power sources.

For recovery, hold BOOT while resetting, release it, and use the
[installer](https://jantielens.github.io/esp32-macropad/) with this exact target.
BOOT on GPIO35 also supports runtime tap/hold actions, but holding it during
reset selects download mode.

## Firmware support and storage

The standard firmware supports Wi-Fi/MQTT, USB/BLE keyboard output, MCP,
remote images, native Extensions, ES8311 speaker/MP3 playback, and ES7210
microphone input, including music analysis. It does not enable a camera.
Persistent files use internal flash.

> [!IMPORTANT]
> This firmware deliberately uses only 16 MB of the physical 32 MB flash for
> compatibility with native Extensions. Do not change the installer/build
> flash-size setting to 32 MB independently of the firmware layout.

Extensions have two 56 KiB slots and one 120 KiB slot of usable ELF capacity.
Use ESP32-P4 packages. OTA is supported after initial full flashing.
Back up files and settings before changing variants or partition layouts.

## Configured onboard signals

These signals are occupied by onboard hardware, not offered as free GPIOs.

| Signal                           | GPIO or address             |
| -------------------------------- | --------------------------- |
| BOOT                             | 35, active low              |
| Panel reset / backlight          | 27 / 26, backlight low-on   |
| Shared I2C SDA / SCL             | 7 / 8                       |
| Touch reset                      | 23                          |
| GT911 address                    | `0x5D`, alternate `0x14`    |
| ES8311 / ES7210 address          | `0x18` / `0x40`             |
| Audio MCLK / BCLK / LRCK         | 13 / 12 / 10                |
| Speaker data / microphone data   | 9 / 11                      |
| Amplifier enable                 | 53                          |

## References

* [Vendor hardware guide](https://www.waveshare.com/wiki/ESP32-P4-WIFI6-Touch-LCD-4B)
* [Firmware assignments](board_overrides.h)
* [Build and partition selections](../../../config.sh)
* [Hardware index](../../../docs/user/hardware.md)
