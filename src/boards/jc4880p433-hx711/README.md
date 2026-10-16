---
title: JC4880P433 HX711 Coffee Scale
description: HX711 connections and firmware differences for the Coffee Scale target.
---

Target `jc4880p433-hx711` connects a load cell through an HX711 amplifier to the
[JC4880P433](../jc4880p433/README.md). Shared power, display, USB, and audio
details are in the base hardware reference.

## Connect the amplifier

| HX711 connection   | Board connection   |
| ------------------ | ------------------ |
| DOUT / data        | GPIO52             |
| SCK / clock        | GPIO51             |
| Supply             | 3V3                |
| Ground             | GND                |

The two signal pins use the same header positions as the NAU7802 variant, but
the protocols differ. Flash the target matching your ADC. Follow the
amplifier's diagram for the load cell terminals; do not infer them from wire
color. Keep logic at 3.3 V.

Tare and calibrate with a known reference mass before measuring a brew.

## Firmware differences

This variant adds scale calibration, live weight/flow, brew templates and logs,
and a Coffee Scale portal landing area. Persistent files use internal flash.

USB HID is disabled; native USB is configured for hardware CDC serial.
BLE keyboard support, Wi-Fi/MQTT, audio, and inherited camera/Extension features
remain configured. OTA is supported after a full first flash of this target.
Use the base board's download procedure for recovery and back up before
changing variants.

## References

* [Coffee Scale workflow](../../../docs/user/specialized/coffee-scale/README.md)
* [Variant assignments](board_overrides.h)
* [Hardware index](../../../docs/user/hardware.md)
