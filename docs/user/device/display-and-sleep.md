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

To end a maintenance session with one timed deep sleep, use
[Sleep now](updates-and-recovery.md#put-the-device-to-sleep-after-configuration)
under **Device > Operating Mode**. This is separate from display inactivity.

**Offline** operating mode keeps the interactive runtime active without starting
Wi-Fi. It does not put the device or display into deep sleep, and supported
Bluetooth controls remain available. See
[Offline boot and explicit network access](network-and-access.md#boot-offline).

## Set brightness and rotation

Open **Display > Brightness** and adjust the slider. It changes the current
brightness immediately; select **Save** to retain it after reboot.
The allowed minimum depends on the device.
Brightness actions also change the current value without saving it. Add a
**Save brightness** action to retain the selected value across reboots. Sleep
and wake do not change the selected brightness.

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
Use a **Wake display** action for a one-time wake; repeated
movement can also use [camera motion](../media/camera.md#detect-motion).

## Save battery power with automatic shutdown

On supported firmware, **Device > Power Savings** turns the device off after
inactivity while running on battery. This is hardware shutdown, not display
sleep or light sleep.

Enter **Auto-shutdown after inactivity (seconds)** from `0` to `65535`, then select
**Save Power Savings**. Changes apply immediately without reboot. `0` disables
automatic shutdown; the initial value depends on the board.

Before turning off, the device tries to display **Powered off automatically**
using its notification appearance. On e-paper, the notice remains visible after
shutdown. If the notice cannot be displayed within 60 seconds, the device turns
off without it; an e-paper screen may retain an older image.
New activity cancels shutdown while the notice is being prepared. If shutdown
fails, its notice is cleared.

Touch, physical buttons, deliberate portal interaction, authenticated changes,
and MCP tool calls restart the inactivity interval. MQTT traffic and background
portal polling do not. External power suspends shutdown; removing it starts a
fresh interval. Unknown power readings do not trigger shutdown.

Setup and configuration recovery remain available and use their existing
recovery sleep policy. Firmware updates, protected configuration writes, and
pending action sequences defer automatic shutdown. Uploads are not tracked as
keep-awake operations. On devices with SD-primary storage and automatic
shutdown, protected writes defer shutdown until they finish or the upload
disconnects. An unfinished voice recording is discarded during shutdown.
Other uploads can be interrupted and leave an incomplete file.
Running timers, sounding alarms, and playback do not keep the device on.
Shutdown does not save portal drafts or
restore a timer, screen, or reading position on the next normal startup.

Use your [board's power-on controls](../hardware.md) to restart. Touch alone
does not necessarily power on a device after hardware shutdown. E-Paper Frames
use their separate scheduled refresh-and-sleep workflow, not this setting.

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
