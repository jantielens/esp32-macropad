---
title: Countdowns and stopwatches
description: Build timer buttons, configure expiry actions, prepare adjustable targets, and display timer state.
---

Timers provide countdowns and stopwatches for tracking time or running actions
when a countdown expires. Interactive display firmware provides three
independent timer slots.
Runtime values do not survive reboot. The portal stores expiry actions;
Timer actions on your pad choose mode, duration, and live controls.

## Build a countdown

1. In **Pads > Pad Editor**, select a button and add a **Timer** tap action.
2. Select the command for the intended timer, such as **T1 Start**.
3. Choose **Countdown** and enter a positive whole-second duration, such as
   `240`. Duration can also use a binding that resolves to valid seconds.
4. Put `[timer:1;mm:ss]` in a label and **Save Pad**.
5. Show the saved pad and tap the button. Check the countdown on the device.

For a stopwatch, select **Stopwatch (Count Up)** instead; it needs no duration.

## Choose controls

| Command          | Behavior                                                                                              |
| ---------------- | ----------------------------------------------------------------------------------------------------- |
| Toggle           | Start a stopped timer with its configured mode/duration; pause a running timer; resume a paused timer |
| Start            | Apply its configured mode/duration and start fresh in every state                                     |
| Stop             | Stop and reset to zero or the countdown target                                                        |
| Pause / Resume   | Freeze or continue the current run                                                                    |
| Reset            | Return to zero or target without changing running state                                               |
| Set countdown    | Set an absolute target in seconds                                                                     |
| Adjust countdown | Add/subtract seconds from the target                                                                  |

Start always applies its own duration. Toggle applies that duration only
when stopped, not when resuming a paused/prepared countdown.

Set/Adjust can prepare a paused countdown from a stopped stopwatch. For example,
**Adjust countdown** `20` prepares a 20-second target; Toggle or Resume then
starts that prepared target even if Toggle's configured duration is `1`.
Reset returns to the prepared or adjusted target.
Use signed seconds such as `15`, `-10`, or Numeric Rocker `{step}` for adjustments.

## Configure expiry actions

Open **Actions > Timers**. For each slot, configure up to three actions, then
select **Save Timer Config**. For example, add a Sound alert with tone pattern
`1000:300 200 1000:300`, or an MQTT publish action.
Audio/network actions require their corresponding configured capability.

A countdown copies the expiry list when it starts. A prepared countdown copies
it when Toggle or Resume starts it. Editing the list does not change an
already running or paused run. Stop and start the timer to use the new list.
There is no reboot requirement for saving expiry settings.

The timer continues below zero after expiry, showing overtime rather than
stopping automatically. Use Stop or Reset when that is not desired.

## Display values and state

Replace `1` with timer `2` or `3` where needed:

| Binding              | Value                                                       |
| -------------------- | ----------------------------------------------------------- |
| `[timer:1]`          | Raw seconds with a decisecond, suitable for numeric widgets |
| `[timer:1;mm:ss]`    | Minutes and seconds, such as `4:05`                         |
| `[timer:1;hh:mm:ss]` | Hours, minutes, and seconds                                 |
| `[timer:1;mm:ss.d]`  | Minutes/seconds with deciseconds                            |
| `[timer:1;ss]`       | Whole seconds                                               |
| `[timer:1_state]`    | `running`, `paused`, or `stopped`                           |
| `[timer:1_expired]`  | `ON` after crossing zero, otherwise `OFF`                   |
| `[timer:1_mode]`     | `up` or `down`                                              |
| `[timer:1_target]`   | Active countdown target in whole seconds                    |

Overtime values are negative. The target updates after Start, Set, and Adjust;
ordinary ticking, Stop, and Reset leave it unchanged. Count-up and unconfigured
timers report target `0`. Use the target binding as a gauge maximum when useful.

Saving a pad or expiry configuration does not persist the current timer's
runtime state, mode, duration, or elapsed time. For a schedule that must recover
across reboot, review [alarms](alarms.md) instead.

[Device tasks](README.md) | [User guides](../README.md)
