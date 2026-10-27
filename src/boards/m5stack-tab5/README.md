---
title: M5Stack Tab5
description: Hardware requirements and firmware limitations for the M5Stack Tab5.
---

Target `m5stack-tab5` uses the ESP32-P4 Tab5 with 16 MB flash, 32 MB PSRAM,
and a 5-inch 720 x 1280 MIPI-DSI display. This is initial bring-up firmware;
physical validation is required.

## Display revision

This target supports the ST7121 and ST7123 integrated display and touch
controllers. It does not support the older ILI9881C display with GT911 touch.
Check the label on the back of the device against the
[vendor revision information](https://docs.m5stack.com/en/core/Tab5). If the
controller cannot be identified, the firmware leaves the display off.

The LCD reset is controlled by an I2C expander. It must not be driven high:
the firmware releases it as an input with a pull-up, following the vendor's
reset guidance. Do not connect external equipment to occupied board signals.

## Storage and recovery

This target stores files in internal flash and does not require an SD card.
The [SD-primary variant](../m5stack-tab5-sd/README.md) instead requires a
readable card at boot. Switching variants does not migrate files. Back up files
and configuration before changing targets or partition layouts; a changed
partition table requires a full USB flash rather than an OTA application update.

For download mode and factory recovery, follow the
[vendor instructions](https://docs.m5stack.com/en/core/Tab5). Do not replace
the C6 wireless firmware without first confirming the recovery procedure.

## Configured features

The firmware enables touch, Wi-Fi through the ESP32-C6, MQTT, MCP, remote
images, and ES8388 speaker playback. Microphone capture, camera, BLE, USB HID,
native extensions, battery monitoring, and charging control are not enabled.
Charging and battery behavior have not been verified by this firmware; follow
the vendor's power and charging guidance before relying on battery operation.

| Signal | GPIO or controller |
| --- | --- |
| Shared I2C SDA / SCL | 31 / 32 |
| Display backlight / touch interrupt | 22 / 23 |
| Display / touch reset | I2C expander `0x43`, bits 4 / 5 |
| Speaker amplifier | I2C expander `0x43`, bit 1 |
| ES8388 MCLK / BCLK / LRCK / data | 30 / 27 / 29 / 26 |
| C6 SDIO CLK / CMD / D0-D3 / reset | 12 / 13 / 11, 10, 9, 8 / 15 |
| C6 power | I2C expander `0x44`, bit 0 |

## References

* [M5Stack Tab5 documentation](https://docs.m5stack.com/en/core/Tab5)
* [Board assignments](board_overrides.h)
* [Hardware index](../../../docs/user/hardware.md)