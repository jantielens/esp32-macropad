---
title: Set up and calibrate a Shutter Tester
description: Match sensor geometry, establish a dark baseline, align illumination, and capture a first shutter result.
---

Shutter Tester measures the exposure pulse seen by optical sensors at the
camera's film plane. Captures compare exposure with an intended shutter speed,
and multiple sensors can reveal variation across the measured positions.

Use [board-local hardware notes](../../hardware.md) for wiring, sensor channel
order, film-plane placement, physical controls, and electrical limits.
Do not put a sensor assembly in the path of moving curtains or touch them.
This is an optical measurement workflow, not repair or a camera safety system.

## Configure sensors

In the Shutter Tester configuration, choose the topology actually installed:
single sensor, three-sensor line, or four-corner array.
Enter measured X/Y offsets in millimeters from the array center.
Four-corner diagnostics need valid geometry and all four usable channels;
selecting a preset does not create missing hardware.

Save settings, then use the portal's pending-reboot control to apply them.
Although the section's button may say Save & Reboot, its normal save stages
the reboot rather than immediately restarting the device.
Verify the running topology after reboot.

## Calibrate dark and align light

1. Close the shutter/remove test illumination and keep the sensor array still.
2. Run Calibrate for the dark baseline, allowing roughly one second.
3. Recalibrate if ambient darkness or the setup changes.
4. Enter Align mode and use steady, even illumination to compare channels.
5. Exit alignment and return to capture.

Calibration is ignored during active capture.
Avoid flickering/PWM illumination and unequal channel lighting.
Alignment percentages help find a usable signal; they do not establish timing
accuracy or certify a camera.

## Capture a first result

Choose the intended target speed, lock it when comparing to that nominal value,
and fire the shutter. Without a lock, the tester chooses a nearby standard
target from the measured result; that can hide an intended-speed mistake.
Target adjustment after a shot changes its comparison, not its captured pulse.

Inspect [waveform and device controls](display.md), then
[interpret the result](results.md). Repeat a few shots before drawing conclusions.
For a speed series, use [sessions and test scripts](sessions.md).

Save/export important sessions and verify their record in the portal.
Treat missing channels, weak pulses, clipped captures, and strange repeated
skew as setup problems to investigate, not automatic evidence of curtain damage.

[Specialized workflows](../README.md) | [User guides](../../README.md)
