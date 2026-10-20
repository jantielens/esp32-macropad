---
title: JC4880P433 NAU7802 Coffee Scale
description: Load-cell ADC connections and firmware differences for the NAU7802 Coffee Scale target.
---

Target `jc4880p433-nau7802` adds a load cell through a NAU7802 ADC to the
[JC4880P433](../jc4880p433/README.md). Use that reference for shared display,
power, USB, and audio connections.

## Connect the scale ADC

| ADC connection   | Board connection   |
| ---------------- | ------------------ |
| SDA              | GPIO52             |
| SCL              | GPIO51             |
| Supply           | 3V3                |
| Ground           | GND                |

Use the breakout's wiring diagram for the load cell's excitation and signal
terminals; wire colors are not universal. Keep logic at 3.3 V. This secondary
sensor connection is not the display/touch I2C bus on GPIO7/GPIO8.

Calibrate with an empty platform and a known reference mass before trusting
weight readings.

## Firmware differences

The portal prioritizes Coffee Scale and brews. Firmware adds scale calibration,
weight/flow data, guided brew templates, and brew logs. Files use internal
flash, not the SD-primary backend.

USB HID is disabled in this variant. The native USB connection is configured
for hardware CDC serial rather than the standard Macropad's HID transport.
BLE keyboard support, Wi-Fi/MQTT, audio, and the inherited camera/Extension
features remain configured.

First flash and recovery use the base board's download procedure with this
exact target. OTA is supported. Back up files before switching variants.

## References

* [Coffee Scale workflow](../../../docs/user/specialized/coffee-scale/README.md)
* [Variant assignments](board_overrides.h)
* [Hardware index](../../../docs/user/hardware.md)
