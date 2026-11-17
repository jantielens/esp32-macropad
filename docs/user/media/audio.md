---
title: Audio and Voice Notes
description: Configure audio feedback, manage sounds and music, and record local voice notes.
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

## Record and manage voice notes

On supported firmware, choose **Voice notes > Recording** when adding an action
to a button or another action host. The default command, **Toggle recording
(start/save)**, starts a new recording when idle. Press the same button again
to stop and save it. Toggle also saves a paused recording.

Use **Start recording**, **Pause recording**, **Resume recording**,
**Stop and save recording**, or **Discard recording** for separate controls.
Commands are rejected while the recorder is starting, saving, or discarding;
wait for the transition to finish before pressing again.
Save completes before subsequent actions run;
failed commands stop the remaining actions. Pause retains the microphone and
does not add silence to the recording. The microphone level meter is unavailable
while recording or paused.

Open **Voice Notes > Notes** to play, seek, download, or delete saved WAV files.
Notes are grouped by date; recordings made before the device clock is set appear
under **Unknown date**. Delete requires confirmation. The portal does not use
your browser's microphone. Recordings use the device input's native 16-bit PCM
format and stay on its selected storage backend. Free space limits recording
time; the device saves captured audio when it reaches the storage reserve.

To process notes, open **Voice Notes > Settings**. Configure the Transcription
Service with an Azure resource endpoint, deployment name, and API key. To
generate titles and summaries from saved transcripts, also configure the LLM
Service with a deployment that supports strict structured outputs. For each
service use the HTTPS resource root, such as
`https://<resource>.openai.azure.com/`, without an `/openai/` path, project URL,
or query. Select **Save**; a reboot is not required. Leave either key blank on
later saves to keep its stored value.

Enable **Automatically process saved notes** to include existing and future
notes with missing results. Automatic processing waits for Wi-Fi and other
required resources; it does not connect Wi-Fi or keep the device awake. It
retries each failing service after 30 seconds, 1 minute, and 5 minutes, then
pauses automatic retries. Save another recording or start processing manually
to begin a fresh cycle. Changing that service's settings also resets its cycle.
Provider cooldowns remain in effect; new recordings do not clear access or
configuration errors. Correct service settings if processing is blocked.

Alternatively, add a **Voice notes > Process saved notes** action to start a
best-effort pass without enabling automation. An offline or busy press schedules
nothing; press it again when the device is ready. This action starts processing
and does not wait for results to finish.

Processing pauses for two seconds after each operation before continuing,
giving other device tasks time to run. The overall pass still shows as running.

In **Notes**, select **Process note**. Open **View details** to play the note and
read its summary and transcript. Transcription sends the recording to Azure;
title and summary generation sends the saved transcript. Keep recordings,
downloads, and credentials protected. A failed attempt leaves saved results
intact. Use **More actions** in details to replace a transcript or regenerate a
title and summary. These actions may incur service charges; retranscribing does
not update an existing title or summary. Recording takes priority over processing.
Access and deployment errors direct you to check Voice Notes Settings. Upload
limit errors require a smaller recording; rate or quota errors require checking
the Azure quota or retrying later.

An empty transcription response is saved successfully as **No speech detected**.
Enrichment follows the normal flow and may produce an empty summary. Completed
results are not automatically retried; use **Transcribe again** to try again.

Use `[voice_note:recording.state]`, `[voice_note:recording.duration_s]`,
`[voice_note:last_saved.id]`, and `[voice_note:recording.error]` on pads to
show recording status and the latest result. Transcription status is available
through `[voice_note:transcription.state]`, `[voice_note:transcription.note_id]`,
and `[voice_note:transcription.error]`.
Use `[voice_note:enrichment.state]`, `[voice_note:enrichment.note_id]`, and
`[voice_note:enrichment.error]` for title and summary generation.
Use `[voice_note:transcription.retry_status]` and
`[voice_note:enrichment.retry_status]` for each service's retry cycle: empty
before retries, **Retry 1 of 3**, **Retry 2 of 3**, **Retry 3 of 3**, or
**Retries halted**. These describe the service, not an individual note.
Retry history resets on reboot.
Use `[voice_note:processing.state]` to show whether background processing is
idle, waiting, running, or blocked.
The latest saved ID remains available until the device restarts. A reboot, firmware update,
factory reset, or device shutdown discards an unfinished recording. If a
manual update or reset reports that recording is stopping, retry the operation.
If microphone cleanup fails, retry **Start** after the issue clears.

## Show a loaded voice note on a pad

On display firmware with PSRAM, you can open a note from a List and show
its saved details on another pad:

1. Configure a [List](../widgets/list.md) with the recent Voice Notes source.
2. Add **Voice notes > Load saved note** as the first item action and set **Note ID** to
   `[item:id]`. Add **Navigate to screen** as the next action and choose the
   detail pad.
3. Add labels or widgets on the detail pad using, for example,
   `[voice_note:loaded.status]`, `[voice_note:loaded.title|Untitled]`,
   `[voice_note:loaded.summary|No summary]`, and
   `[voice_note:loaded.transcript|No transcript]`. Save both pads.
4. Activate a List item. Navigation starts immediately while the note loads.
   The status is `loading`, then `ready` or `error`; use
   `[voice_note:loaded.error|]` to show a failure.

The loaded ID, creation time, duration, and transcript/enrichment availability
are also available in the Binding Reference. Missing optional results use the
configured fallback. A rejected ID stops the action list; later read errors
appear in the loaded status instead.

To delete a saved note from a List, add **Voice notes > Delete saved note** and set **Note ID**
to `[item:id]`. Deletion is permanent and also removes the note's transcript
and enrichment. The action waits for deletion to finish; if the note is busy
or deletion fails, later actions do not run. To ask for confirmation, capture
`[item:id]` with **Set variable**, then add **Confirm** and **Delete saved note**
using `[var:name]`. The List refreshes after deletion.

When the action deletes the loaded note, its details clear and loaded status
returns to `idle`. Deleting a different note leaves the loaded details unchanged.
Deleting through the web portal does not clear the loaded snapshot.

Use labels for details that must not show a previous note. Some widgets retain
their last value while a binding is unavailable or animate from that value;
historical charts can also include samples from previous notes. Check the loaded
status and ID before relying on widget values.

One loaded note is shared by all pads. Its saved title, summary, and transcript
remain unchanged until another Load, a matching **Delete saved note** action,
or reboot, even if the source note is edited or deleted through the portal.
Labels show short UTF-8-safe previews, not a scrolling reader. Checked action
fields reject values that do not fit rather than using a truncated prefix.
Loading defers while recording or a firmware update needs the device.

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
