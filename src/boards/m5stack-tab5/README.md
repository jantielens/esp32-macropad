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

The firmware enables touch, Wi-Fi and BLE keyboard HID through the ESP32-C6,
MQTT, MCP, remote images, native extensions, ES8388 speaker playback, battery
monitoring, and charging. Microphone capture, camera, BLE telemetry, and USB
HID are not enabled. BLE keyboard pairing and Wi-Fi coexistence require physical
validation on this board.

USB-C provides serial diagnostics through USB CDC. USB HID is unavailable:
the firmware's USB HID stack uses the controller connected to the host-oriented
USB-A port, not USB-C. Remote logging remains available. Native extensions
require physical validation on this board.

## Speaker audio

To avoid pops at silence boundaries during tone and MP3 playback, open
**Audio > Volume**, turn off **Mute amplifier when idle**, and **Save**.
The setting applies without reboot. The firmware keeps the amplifier enabled
and continuously feeds a very low-level signal through the ES8388 between
sounds. Idle muting remains available, but switching the amplifier can still
produce pops. Firmware updates and reboot shutdown can still mute or stop audio
regardless of this setting.

See [Feedback, sound alerts, and Music](../../../docs/user/media/audio.md) for
volume, wake delay, tone patterns, and MP3 playback controls.

## Battery and charging

The firmware enables the vendor's 500 mA charging setting with quick charge
disabled during board initialization. Charging stops when the board is off.
The firmware does not provide a charging control or a battery percentage.
Charging and battery behavior have not been physically verified: monitor
current and battery temperature during initial testing and follow the
[vendor power and charging guidance](https://docs.m5stack.com/en/core/Tab5).

The INA226 reports battery voltage and signed current, positive when charging
and negative when discharging. Use `[sensor:battery_voltage;%.2f|N/A]` and
`[sensor:battery_current;%.2f|N/A]` in a button, or use the corresponding
Home Assistant sensors with MQTT. Failed samples can retain their last valid
values; see [local sensor readings](../../../docs/user/pads/data-sources.md#read-local-sensors-and-health).

| Signal | GPIO or controller |
| --- | --- |
| Shared I2C SDA / SCL | 31 / 32 |
| Display backlight / touch interrupt | 22 / 23 |
| Display / touch reset | I2C expander `0x43`, bits 4 / 5 |
| Speaker amplifier | I2C expander `0x43`, bit 1 |
| ES8388 MCLK / BCLK / LRCK / data | 30 / 27 / 29 / 26 |
| C6 SDIO CLK / CMD / D0-D3 / reset | 12 / 13 / 11, 10, 9, 8 / 15 |
| C6 power | I2C expander `0x44`, bit 0 |
| Charging enable / quick-charge disable | I2C expander `0x44`, bits 7 / 5 |
| Battery voltage and current monitor | INA226 at I2C `0x41` |

## References

* [M5Stack Tab5 documentation](https://docs.m5stack.com/en/core/Tab5)
* [Board assignments](board_overrides.h)
* [Hardware index](../../../docs/user/hardware.md)