---
title: Audio and voice clips
description: Configure audio feedback, manage sounds and music, and record local voice clips.
---

Audio provides button feedback, alerts, and Music playback, with microphone
features available separately. These uses share audio resources, so playback
priority matters when more than one feature requests sound.

Audio controls appear only when supported by the installed firmware.
Speaker output, MP3 playback, microphone input, and music analysis are distinct
capabilities. Check the [hardware reference](../hardware.md) for connections.

## Set volume and amplifier behavior

Open **Audio > Volume** and adjust the slider from 0-100%. The change applies
immediately; select **Save** to retain the current volume across reboots.
The device volume applies to feedback, alerts, and Music.
Volume actions and MQTT volume commands change the current volume without
saving it. Add a **Save volume** action to retain it across reboots. Each save
action stores only its own level.

When an amplifier section is shown, **Mute amplifier when idle** defaults on
to reduce hiss. Turning it off keeps playback enabled but can restore idle
hiss. **Amplifier wake delay** accepts 0-1000 ms, default 120 ms.
Lower delay reduces latency but can clip the beginning of a short sound.
Zero skips the wait.

The delay is editable only with idle muting enabled. Its saved value is retained
when disabled and can still apply after startup/update when the amplifier
actually needs to wake. Already-awake playback has no extra wake delay.
Save both settings; no reboot is required.

## Write a tone pattern

A tone pattern is a space-separated sequence of `frequency:duration` pairs.
Frequency is in Hz; duration is in milliseconds. A bare duration is silence:

```text
800:80
600:40 40 600:40
1000:100 200 1200:100
```

The last example plays 100 ms, pauses 200 ms, then plays 100 ms.
Use this language for feedback, Tone Alerts, and Alarm Loop Tone patterns.
Bindable tone fields can resolve a pattern from live data.

In **Audio > Button Feedback**, enter Tap Feedback and Long-Press Feedback
patterns, then **Save**. Empty disables that feedback.
Feedback is limited to one second total, including gaps; longer patterns are
truncated. Explicit tone alerts do not use this feedback limit.

A button without actions for an interaction does not flash or beep for it.
A Sound alert action suppresses that interaction's default beep.
Swipe feedback uses the same suppression rule.

## Upload an alert sound

Open **Audio > Alert Sounds**, choose an MP3, set its sound name, and
select **Upload**. Names use letters, digits, hyphens, and underscores.
Each alert file is limited to 512 KiB and its MP3 header is validated on upload.
Use Play to test and Delete to remove it.

In a button, swipe, or boot action, choose **Sound alert > MP3 Alert** and
select the uploaded sound. Tone Alert uses the pattern syntax above.
Where offered, a volume override applies to that action.
Save the pad or action host separately from the file upload.

Use **Alarm > Loop Tone** or **Loop MP3** for a repeating alarm source.
Pair it with stop controls and review [alarm behavior](../device/alarms.md);
a one-shot sound alert is not a scheduled alarm.

## Manage a Music library

Open **Audio > Music Library** to upload/delete MP3 tracks under `/media`.
Nested folders are supported. The catalog contains up to 32 files in a stable
sorted order. If more exist, an overflow warning appears; deleting a listed
track reveals another on the refreshed list.
ID3 metadata can supply title/artist, and durations can be estimates.

The library view manages files; it does not provide playback, seek, reorder,
or track-selection controls. Upload/delete is blocked while Music is playing,
paused, or interrupted, or while an MP3 Alert is active.
Stop those sources first. Tone Alerts alone do not block library management.
The list refreshes after successful changes.
Upload success confirms stored bytes, not a full decode of the track.

Add **Music** actions to a pad for Play/Pause, Next track, Previous track,
and Stop. Save the pad and test it on the device.
Next/Previous change the selected track without forcing playback.
After interruption, Music restarts the selected track from the beginning if
its desired state is still Playing; it is not sample-accurate resume.

## Record and manage voice clips

On supported firmware, add **Voice clip** actions to buttons or other action
hosts. Use **Start**, **Pause**, **Resume**, **Save**, and **Discard** to
control one local recording. Save completes before subsequent actions run;
failed commands stop the remaining actions. Pause retains the microphone and
does not add silence to the clip. The microphone level meter is unavailable
while recording or paused.

Open **Audio > Voice clips** to play, seek, download, or delete saved WAV files
in your browser. Clips are grouped by date; recordings made before the device
clock is set appear under **Unknown date**. Delete requires confirmation.
The portal does not use your browser's microphone. Clips use
the device input's native 16-bit PCM format and stay on its selected storage
backend. Free space limits recording time; the device saves captured audio when
it reaches the storage reserve. Keep private recordings and downloads protected.

Use `[recorder:state]`, `[recorder:duration_s]`, `[recorder:last_clip]`, and
`[recorder:error]` on pads to show recording status and the latest result.
Starting a new recording clears the previous result. A reboot, firmware update,
factory reset, or device shutdown discards an unfinished recording. If a
manual update or reset reports that recording is stopping, retry the operation.
If microphone cleanup fails, retry **Start** after the issue clears.

## Understand sound priority

Scheduled alarm audio has priority over ordinary alerts, then Music.
Ordinary alerts cannot replace an alarm. On PCM audio devices, short
tap/confirmation feedback can overlay the active source. Single-pitch buzzer
devices discard feedback while an alert is active and clear any current
feedback when an alert starts.

**Stop alerts** leaves Music intent and scheduled ringing ownership unchanged.
**Stop all audio** also clears Music intent and feedback, but does not disable
future scheduled alarms. Use Alarm Cancel/Snooze to end a scheduled ringing
session. Firmware updates stop playback without automatic Music restart.

## Put playback state on a pad

Useful label bindings include:

```text
[music:title]
[music:artist]
[music:index] / [music:count]
[music:status]
[music:elapsed_s] / [music:total_s]
```

Status distinguishes playing, interrupted, paused, stopped, empty, unavailable,
and error. Track count/duration can be unavailable; use the Binding reference
before treating negative sentinel values as valid measurements.

On firmware with music analysis, `[music:analysis.rms]`,
`[music:analysis.peak]`, and `[music:analysis.band.0]` through band `7` report
0-100 pre-volume levels. Analysis is demand-driven by configured bindings.
It analyzes track audio, not microphone capture.

Keep MP3 originals in a [separate backup](../device/backup-and-restore.md).
Deleting a file can leave action references pointing to a missing sound.

[Media tasks](README.md) | [User guides](../README.md)
