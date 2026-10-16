---
title: ESP32-C3 SuperMini Sensor Node
description: Synthetic sensor defaults, BLE telemetry, and configuration recovery for the C3 sensor target.
---

Target `esp32c3-withsensors` is a headless ESP32-C3 SuperMini with 4 MB
flash and no PSRAM. It has no display or pad UI.

## What the shipped target measures

> [!IMPORTANT]
> This target enables a **synthetic dummy sensor**, not a real temperature or
> presence sensor. BME280 and LD2410 adapters are disabled. A sensor connector
> or pin assignment does not enable its firmware adapter.

Telemetry uses BTHome v2 BLE advertising. MQTT, MCP, BLE keyboard output,
audio, and runtime hardware-button actions are disabled.
The USB connection is for flashing and native CDC serial, not USB HID.

The firmware contains inactive mappings for I2C SDA GPIO8/SCL GPIO10 and
LD2410 OUT GPIO4. They are not a supported plug-and-play sensor configuration.
Enabling real sensors requires a matching firmware configuration and checked
wiring; do not mistake dummy readings for measurements.

## Configuration and recovery

The configuration access point accepts one client at a time.
For portal recovery, power the device on, switch it off before ten seconds
have elapsed, then power it on again. Two consecutive power-on boots before
the counter clears enter configuration mode. Ordinary software resets are
not the same trigger.

GPIO9/BOOT is also configured as an active-low startup recovery input. Holding
it during reset can enter the chip's download mode instead, so use the
power-cycle sequence for portal access. For USB flash recovery, deliberately
use BOOT/reset download mode and the
[installer](https://jantielens.github.io/esp32-macropad/) with this exact target.

## Storage and updates

The firmware uses NVS settings but has no filesystem or storage browser.
Do not expect stored pads, uploaded files, or native Extensions.
Its two-application partition layout supports OTA after initial full flashing.
Back up settings before replacing firmware or changing the partition layout.

## References

* [Firmware assignments](board_overrides.h)
* [Build and partition selections](../../../config.sh)
* [Hardware index](../../../docs/user/hardware.md)
