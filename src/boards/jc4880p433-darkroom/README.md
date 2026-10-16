---
title: JC4880P433 Darkroom Timer
description: Light-meter connections, relay requirements, and Darkroom Timer firmware differences.
---

Target `jc4880p433-darkroom` uses the
[JC4880P433 hardware](../jc4880p433/README.md) with an external TSL2591
light sensor. Shared power, display, USB, audio, and recovery details are in
the base reference.

## Connect the meter

| TSL2591 connection   | Board connection   |
| -------------------- | ------------------ |
| SDA                  | GPIO52             |
| SCL                  | GPIO51             |
| Supply               | 3V3                |
| Ground               | GND                |

The sensor uses address `0x29` on the secondary I2C bus at 400 kHz.
Keep its logic at 3.3 V. Place the probe where it can measure the projected
image at the easel.

## Relay and power requirements

Enlarger and safelight switching uses configured Shelly relays over Wi-Fi.
There are no firmware-assigned relay output GPIOs on this controller.

> [!WARNING]
> Do not connect enlarger or safelight mains wiring to this board.
> Use appropriately rated, safely installed switching equipment.

Configure relay addresses/channels in the Darkroom Timer portal and calibrate
the paper/metering setup before relying on exposure recommendations.

## Firmware differences

The portal prioritizes Darkroom Timer controls. Firmware adds exposure and
test-strip timers, metering, and print logs. Persistent files use internal flash.

USB HID is disabled; native USB is configured for hardware CDC serial.
BLE keyboard support, Wi-Fi/MQTT, audio, and inherited camera/Extension features
remain configured. First flash/recovery use this target's full image and the
base board's download procedure. Later updates can use OTA.

## References

* [Darkroom workflow](../../../docs/user/specialized/darkroom-timer/README.md)
* [Variant assignments](board_overrides.h)
* [Hardware index](../../../docs/user/hardware.md)
