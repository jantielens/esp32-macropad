---
title: Schedule and control an alarm
description: Configure weekly or one-time alarms, ring and stop actions, pad controls, and missed-alarm behavior.
---

An alarm schedules a reminder at a local time, either once or on selected
weekdays, and runs configured actions when it rings.
Alarm-capable firmware provides one alarm, slot 1. It is disabled by default.
Set and verify [device time](time.md) before choosing an alarm time.

## Set the schedule

1. Open **Alarm > Schedule**.
2. Choose a time in the device timezone and enable the alarm.
3. Select weekdays for a weekly schedule, or leave every day unchecked for a
   one-time alarm.
4. Select **Save schedule**. Check readiness, warnings, and the scheduled date
   shown for a one-time alarm.

A one-time alarm selects the next occurrence, usually today or tomorrow, and
disables itself when it rings. Snooze can still ring again within that session.
Enable and save again for another one-time alarm.
An identical save keeps its existing scheduled instant.

## Set ringing behavior

Open **Alarm > Behavior** and configure:

* Snooze duration, 1-1440 minutes; the default is 9
* Automatic dismissal, 1-1440 minutes; the default is 30
* Up to three actions when ringing starts
* Up to three actions when ringing stops, including snooze and dismissal
* Maximum alarm lateness, described below

Select **Save behavior**. Schedule and Behavior save independently and apply
without reboot. Ring/stop lists allow synchronous actions; actions that suspend
execution, such as Delay or key sequences, are not offered.

For sound-capable firmware, choose **Alarm > Loop Tone** or **Loop MP3** in
the ringing action list. A useful stop action is **Sound alert > Stop alerts**.
Tone patterns and sound files are covered in [audio](../media/audio.md).
MP3 repetition is not guaranteed gapless; failed MP3 alarm playback attempts
a bounded fallback tone.

## Provide Snooze and Cancel controls

The settings forms are not live ring-control panels and do not continuously
poll alarm state. Add **Alarm Control** actions to buttons on a pad:

* Snooze ends the current ring, runs stop actions, and rings again after the
  configured snooze duration.
* Cancel ends ringing or snooze without disabling a weekly schedule.
* Target the active alarm or slot 1 for these controls. An idle control does
  nothing.

Scheduled alarm audio takes priority over ordinary alerts and Music. Stopping
audio alone does not dismiss the ringing session. Use **Alarm Control > Cancel**
to end it. **Stop all audio** also clears Music intent and feedback, but does
not disable future scheduled alarms. Interrupted Music restarts its remembered
track from the beginning only if it is still meant to be playing.

## Change the schedule from a pad

Other Alarm Control commands configure slot 1:

| Command                         | User-entered value                                                   |
| ------------------------------- | -------------------------------------------------------------------- |
| Set Time                        | Whole minutes since midnight, 0-1439                                 |
| Adjust Time                     | Signed whole minutes, such as `60`, `-1`, or Numeric Rocker `{step}` |
| Enable, Disable, Toggle         | No value                                                             |
| Weekday enable, disable, toggle | Select the weekday                                                   |

Time adjustments wrap within 24 hours without changing weekdays or enabled
state. Removing every weekday selects one-time mode. Durations and ring/stop
lists remain in the portal's Behavior form.

Pad changes apply live, then save after ten seconds without a substantive
change. Repeated adjustments restart that delay. Power loss or reboot before
the save succeeds can lose those edits. A successful substantive change
dismisses ringing/snooze using the old stop actions and schedules from the
next full minute; identical changes do not.

For feedback, use label bindings such as:

```text
Alarm [alarm:1_time]
[alarm:1_state]
[alarm:1_save_state]
[alarm:1_command_message]
```

State is `idle`, `ringing`, or `snoozed`. Save state distinguishes pending,
saved, and failed changes. The Binding reference also exposes readiness,
next-ring/snooze countdowns, and availability flags; use those flags rather than
assuming zero means a valid scheduled instant.

## Missed alarms and reboots

The alarm waits for an NTP synchronization during the current boot. Once
synchronized, Wi-Fi loss does not stop the clock. **Maximum alarm lateness**
applies both after reboot and to delays while running.

The default is 360 minutes; accepted values are 0-10080 whole minutes.
Zero permits only on-time delivery. An occurrence at the exact allowed boundary
can ring, but a later one cannot. Lateness is evaluated when reliable time is
available, not when power was restored. Only the latest eligible occurrence
rings; days without power do not replay a backlog.

A saved one-time instant survives reboot. Within the lateness window it can
ring once; outside it, it disables without rescheduling. An alarm saved before
time synchronization remains pending until the clock is ready.
Ringing and snooze sessions do not survive reboot.

Enabling or substantively editing a schedule excludes earlier occurrences and
arms from the next full minute. Changing only maximum lateness preserves an
active session. [Timezone changes and daylight saving](time.md#alarm-and-daylight-saving-effects)
have additional scheduling consequences.

> [!WARNING]
> Definitions and handled-occurrence records depend on the primary filesystem.
> Storage loss/reset can lose the alarm. If saving a handled occurrence fails,
> delivery still takes priority; a reboot before a successful retry can repeat
> it. Check readiness/storage/action warnings and test your controls before
> relying on an alarm.

Failed or rejected explicit portal saves preserve the previous definition and
active session. Failed delayed pad saves keep the live change and retry;
firmware updates can defer writes.

[Device tasks](README.md) | [User guides](../README.md)
