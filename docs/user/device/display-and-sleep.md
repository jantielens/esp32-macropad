---
title: Display, idle screen, and sleep
description: Set brightness, rotation, and inactivity behavior while preserving reliable wake controls.
---

Display settings control how content is presented and what happens when the
device is idle. Brightness, an idle screen, and sleep serve different purposes:
showing another screen does not necessarily turn off the display or put the
device into low-power sleep.

Use the controls your firmware exposes. Hardware-specific sleep behavior,
frontlight availability, and defaults belong in the
[board reference](../hardware.md), not in a universal display setup.

## Set brightness and rotation

Open **Display > Brightness** and adjust the slider. It changes the current
brightness immediately; select **Save** to retain it after reboot.
The allowed minimum depends on the device.

Open **Display > Rotation** to select 0, 90, 180, or 270 degrees clockwise
relative to the board's default orientation. Zero keeps that default.
Save and reboot to apply the choice to display and touch together.

## Choose an inactivity sequence

Open **Display > Screen Saver** when available.

1. Enable **Show an idle screen** if you want a temporary pad after inactivity.
   Select the pad and its delay.
2. Enable automatic display/backlight shutoff if you also want a second stage.
   Enter its inactivity delay.
3. Set Fade Out/Fade In durations, or `0` for an immediate transition.
4. Choose touch wake and any wake/keep-awake binding.
5. Check the timeline, then **Save**. These settings apply without reboot.

Both delays count from the most recent activity, not from the previous stage.
An idle delay of 300 seconds and shutoff at 1800 seconds means five minutes to
the idle pad and 30 minutes total to shutoff, not 35.
A zero delay disables the corresponding timed stage.

Idle Screen is temporary. The first wake interaction is consumed and restores
the screen that was active before idle; it does not activate the button under
that touch or add the idle pad to navigation history.
For direct sleep without an Idle Screen, a pad's **Wake Screen** can select
the screen to show on wake.

## Keep the display awake from a binding

If MQTT is configured, a wake binding can follow a presence signal, for example:

```text
[mqtt:devices/room/presence/state]
```

An `ON` value wakes and leaves Idle Screen/Display Sleep and holds both stages
off while it remains on. The normal inactivity sequence resumes afterward.
Save the screen saver section after entering the binding.
Use a **Device command > Wake display** action for a one-time wake; repeated
movement can also use [camera motion](../media/camera.md#detect-motion).

## Backlight-only and e-paper behavior

If the portal labels the second stage **Backlight Off**, rendering and the
controller stay active. A black screen is not proof of deep sleep or low
power consumption. Use the board reference for the electrical behavior.

Some frontlight-equipped firmware exposes **Turn off backlight after** directly
in Brightness instead of a Screen Saver section. Zero keeps illumination on.
Touch, a wake action, or an `ON` backlight binding restores illumination while
the e-paper image continues updating.

For image frames, use the [E-Paper Frame workflow](../specialized/epaper-frame/README.md):
its refresh-and-deep-sleep schedule is different from interactive display sleep.
Configurable interactive e-paper rendering is under **Display > E-paper**;
mode availability and physical refresh constraints are in the board reference.

Use lower brightness, avoid leaving static high-contrast content illuminated,
and use appropriate sleep controls on emissive displays.
Check the actual result with [Screen Preview](diagnostics.md#inspect-the-current-screen)
and the physical display; a framebuffer preview cannot prove panel power state.

[Device tasks](README.md) | [User guides](../README.md)
