---
title: FireBeetle 2 ESP32-C6 with AHT10
description: AHT10 wiring, battery monitoring, configuration recovery, and USB-only updates for the C6 sensor node.
---

Target `firebeetle2-esp32c6-aht10` is for the DFRobot FireBeetle 2 ESP32-C6
v1.2 with 4 MB flash, no PSRAM, and an external AHT10 temperature/humidity
sensor. It is headless, with no display or pad UI.

## Sensor connections

| AHT10 connection   | Board connection   |
| ------------------ | ------------------ |
| SDA                | GPIO19             |
| SCL                | GPIO20             |
| Supply             | 3V3                |
| Ground             | GND                |

The sensor uses I2C address `0x38`. Follow the breakout's pin labels and keep
logic at 3.3 V. The firmware also measures the onboard LiPo divider on GPIO0
with a 2:1 ratio. That ADC pin is already connected to the battery circuit;
do not attach an external signal to it.

Battery percentage is an estimate from voltage, not a precision charge gauge.
The builtin LED on GPIO15 is left unconfigured by this target.

## Firmware and portal

Wi-Fi/MQTT, MCP, and BTHome BLE telemetry are enabled.
MQTT triggers are limited to three. Audio, USB/BLE keyboard output, runtime
button actions, and the storage browser are disabled.
The USB connection provides flashing and native CDC serial, not HID output.
The configuration access point accepts one client at a time.

For portal recovery, power on, switch off before ten seconds of uptime, then
power on again. Two consecutive power-on boots trigger configuration mode;
software resets are not equivalent. GPIO9/BOOT is also a startup recovery
input, but holding it during reset can select chip download mode instead.

## Firmware updates and power

> [!IMPORTANT]
> This target's current single-application layout does **not** support OTA.
> Use a USB full flash for installation, recovery, and later firmware updates.
> Back up settings before flashing or changing layouts.

Use the [installer](https://jantielens.github.io/esp32-macropad/) with this exact
target and the vendor's BOOT/reset download procedure when needed.
Use the vendor's specified USB/battery connections and charging requirements;
sensor wiring is not a battery-charging circuit.

## References

* [Vendor hardware and power guide](https://wiki.dfrobot.com/dfr1075/)
* [Firmware assignments](board_overrides.h)
* [Build and partition selections](../../../config.sh)
* [Hardware index](../../../docs/user/hardware.md)
