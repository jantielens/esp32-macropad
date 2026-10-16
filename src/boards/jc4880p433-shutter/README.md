---
title: JC4880P433 Shutter Tester
description: Photodiode inputs, preset wiring, MicroSD requirements, and Shutter Tester firmware differences.
---

Target `jc4880p433-shutter` uses the
[JC4880P433](../jc4880p433/README.md) with photodiode measurement inputs.
Shared display, power, and audio connections are in the base hardware reference.

## Storage and input requirements

> [!IMPORTANT]
> Insert a readable FAT32 MicroSD card before boot. This target uses the card
> for persistent files, including sessions and pads, and stops startup if it
> cannot mount it. It does not fall back to internal file storage.

The slot uses four-bit SDMMC at up to 20 MHz, LDO channel 4, and active-low
card power on GPIO45. Power down before removing the card. Switching from the
base firmware does not migrate files to SD.

Use the photodiode rig's verified signal-conditioning circuit. These GPIOs are
ADC inputs, not connections for an external light source's power supply.
The pin table is not a complete photodiode circuit diagram.

| Physical input   | ESP32-P4 GPIO   |
| ---------------- | --------------- |
| ADC S1           | 49              |
| ADC S2           | 50              |
| ADC S3           | 51              |
| ADC S4           | 52              |

Single-sensor mode uses GPIO49. The three-line preset uses GPIO49/50/51.
The four-corner preset changes logical sensor ordering:

| Displayed corner / logical sensor   | Physical input / GPIO   |
| ----------------------------------- | ----------------------- |
| Top left / S1                       | ADC S4 / 52             |
| Top right / S2                      | ADC S3 / 51             |
| Bottom left / S3                    | ADC S1 / 49             |
| Bottom right / S4                   | ADC S2 / 50             |

Select the preset matching the rig in the portal, then save and reboot.
Indirect/offload and L-shape descriptors are not active capture presets.
Do not attach Coffee Scale or Darkroom sensors to these shared GPIO positions
while using this rig.

## Firmware and recovery differences

USB HID and remote image fetching are disabled. Native USB is configured for
hardware CDC serial. The firmware retains Wi-Fi/MQTT, BLE keyboard support,
MCP, audio, camera support, and native Extensions from the base target.
The portal starts at the Shutter Tester session browser.

Use the base board's download procedure with this exact target for the first
full flash or recovery. OTA remains supported. Back up the card and settings
before changing variants or partition layouts.

## References

* [Shutter Tester workflow](../../../docs/user/specialized/shutter-tester/README.md)
* [Current preset mapping](../../app/device_classes/shutter_tester/shutter_capture.cpp)
* [Variant assignments](board_overrides.h)
* [Hardware index](../../../docs/user/hardware.md)
