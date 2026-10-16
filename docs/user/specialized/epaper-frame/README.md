---
title: Set up an E-Paper Frame
description: Connect an image source, verify a manual refresh, and choose safe scheduled sleep without pad-editor steps.
---

Frame firmware displays prepared images and sleeps; it has no interactive pads.
For live pad firmware, use [Interactive E-Paper](../interactive-epaper.md).

## Get a first image

1. Check [board notes](../../hardware.md) for image dimensions/format, power,
   physical wake/recovery controls, and any panel-specific calibration.
2. Complete [Wi-Fi setup](../../getting-started.md) and open the portal.
3. Open **E-Paper Frame > Images & Schedule**.
4. Choose [Direct image URLs or the image service](image-sources.md).
5. Save, then use **Status > Refresh e-paper now**.
6. Inspect the physical image and [refresh result](status.md).
7. Set the [refresh window, cadence, and power budget](schedule-and-power.md)
   before unattended battery use.

Each fragment saves independently. A normal Save does not reboot or necessarily
draw an image immediately; use explicit refresh/reboot controls when required.
The portal needs recovery/configuration mode to remain reachable while the
normal duty-cycle flow otherwise sleeps.

## Configure panel-only options

Status Overlay can show battery icon/percentage, time, and last cycle duration
in a selected corner. These are drawn during panel updates, not a live clock.
Invalid time can show a placeholder until clock sync.
Panel-specific color/rotation capabilities are described by the device.

Frontlight, when available, is for button wakes, not timer wakes.
Brightness 0-63, where 0 disables it, and duration 1-600 seconds control that
temporary illumination.

> [!WARNING]
> If VCOM controls are present, use the panel's printed value and board notes.
> Read current VCOM first. Show test pattern can preview a volatile candidate;
> Write VCOM programs limited-endurance EEPROM. Do not repeatedly write values
> while experimenting or copy another panel's calibration.

## Protect the configuration

Image-service tokens are separate from portal/MQTT credentials.
Blank token input preserves the saved value.
Record the Frame settings and keep image-source originals and credentials
outside the device. Frame firmware has no Pad Editor device-config export;
see [backup scope and separate file backups](../../device/backup-and-restore.md).
Low-battery/error/configuration screens can replace an otherwise retained image;
use [status guidance](status.md) rather than treating all changes as image faults.

[Image sources](image-sources.md) | [Schedule and power](schedule-and-power.md) |
[Status](status.md) | [User guides](../../README.md)
