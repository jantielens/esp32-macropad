---
title: Guition JC1060P470C
description: USB, display, recovery, and SD-variant hardware notes for the JC1060P470C.
---

Target `jc1060p470c` is the standard Macropad for the 7-inch, 1024 x 600
JD9165 MIPI-DSI panel with GT911 touch. Hardware has an ESP32-P4, ESP32-C6
wireless companion, 16 MB flash, and 32 MB PSRAM. The firmware starts in
portrait orientation; display rotation can be configured.

This is also the shared hardware reference for the
[SD-primary variant](../jc1060p470c-sd/README.md).

## USB, power, and recovery

The high-speed native USB-C connector provides keyboard, mouse, and gamepad
output. The USB-UART/download connector provides flashing and diagnostic logs
at 115200 baud. Native USB in the standard target does not provide serial logs.
Keyboard output defaults to Off; select USB or BLE in the portal and reboot.

> [!WARNING]
> The connector power rails are shared. Use one powered cable unless the
> board documentation provides a safe arrangement for simultaneous connections.

For USB recovery, hold BOOT while resetting, release it, then flash this exact
target through the
[installer](https://jantielens.github.io/esp32-macropad/).
Install a full image/partition table on first flash or layout changes; back up
configuration and files first. OTA is available after initial installation.
BOOT also supports configured runtime tap/hold actions.

## Firmware support

The base target stores files in internal flash. It supports Wi-Fi/MQTT,
USB/BLE keyboard transport, remote images, MCP, ES8311 speaker/MP3 playback,
native Extensions, and an OV02C10 camera when fitted.
The Extension partition has two 56 KiB slots and one 120 KiB slot for ESP32-P4
packages (usable ELF capacity).

Display sleep switches the backlight off but keeps the panel powered and
scanning a black image. Do not interpret a dark screen as full panel power-off;
this strategy consumes more idle power than powering down the panel.

## Configured onboard signals

These are occupied firmware assignments, not a spare-pin header diagram.

| Signal                             | GPIO or address            |
| ---------------------------------- | -------------------------- |
| BOOT                               | 35, active low             |
| Panel reset / backlight            | 27 / 23                    |
| Shared I2C SDA / SCL               | 7 / 8                      |
| GT911 reset / interrupt            | 22 / 21                    |
| GT911 address                      | `0x5D`, alternate `0x14`   |
| ES8311 / camera address            | `0x18` / `0x36`            |
| Audio MCLK / BCLK / LRCK           | 13 / 12 / 10               |
| Audio data to codec / amp enable   | 9 / 11                     |

Although an audio input signal is assigned to GPIO20, this target does not
enable microphone capture.

## References

* [Board support reference](https://github.com/cheops/JC1060P470C_I_W)
* [Firmware assignments](board_overrides.h)
* [Build and partition selections](../../../config.sh)
* [Hardware index](../../../docs/user/hardware.md)
